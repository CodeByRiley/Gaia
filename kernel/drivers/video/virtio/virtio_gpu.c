/* kernel/virtio/virtio_gpu.c , virtio-gpu (2D scanout) driver.
 *
 * Owns the host-side resource id + scanout. framebuffer.c owns the
 * kernel-side pixel buffer; they cooperate via create_scanout_2d (attach
 * backing), resize_scanout_2d, and flush_rect (push pixels).
 *
 * Single global instance , the kernel only ever drives one GPU. Submits
 * commands on the controlq, polls for responses synchronously (no
 * interrupt path yet), and exposes display-resize events through
 * virtio_gpu_poll_display_event() so the kernel TTY can rebind on host
 * window resize.
 */
#include <drivers/video/virtio/virtio_gpu.h>
#include <drivers/video/virtio/virtio.h>
#include <pci/pci.h>
#include <memory/pmm.h>
#include <memory/hhdm.h>
#include <memory/vmm.h>
#include <utilities/log.h>
#include <utilities/string.h>
#include <stdint.h>

/* Single global instance. */
static struct virtio_dev vdev;
static struct virtq      controlq;
static struct virtio_gpu gpu_state;
/* Accelerated QEMU renderers may complete controlq work asynchronously. */
static u64 gpu_fence_id;
/* Set when the device offered VIRTIO_GPU_F_VIRGL, i.e. the host is a GL
 * backend. See virtio_gpu_scanout_needs_exact_resource. */
static int gpu_is_gl_backend;

/* Scratch request/response buffers. We do all I/O synchronously, so a single
 * page each is plenty , the largest commands are ATTACH_BACKING with a tail
 * of mem_entry records, which still fit in 4 KiB up to ~340 entries (each is
 * 16 bytes). For framebuffers larger than 1.3 MiB we'll split the attach
 * across multiple commands. */
static u64 scratch_req_phys;
static u64 scratch_resp_phys;
static u8 *scratch_req;
static u8 *scratch_resp;

/* A queued command cannot use the synchronous scratch page: the host may
 * still be reading it while we prepare the next command.  Framebuffer damage
 * is bounded to eight regions, so sixteen request/response page pairs cover
 * one TRANSFER_TO_HOST_2D and one RESOURCE_FLUSH for every region. */
#define GPU_BATCH_COMMANDS (VIRTIO_GPU_MAX_FLUSH_RECTS * 2)
struct gpu_batch_buffer {
    u64 req_phys;
    u64 resp_phys;
    u8 *req;
    u8 *resp;
};
static struct gpu_batch_buffer gpu_batch_buffers[GPU_BATCH_COMMANDS];
static int gpu_batch_ready;

/* virtio-gpu wire structures (subset we use). */
struct gpu_ctrl_hdr {
    u32 type;
    u32 flags;
    u64 fence_id;
    u32 ctx_id;
    u32 padding;
} PACKED;

struct gpu_resp_display_info {
    struct gpu_ctrl_hdr hdr;
    struct virtio_gpu_display_one pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} PACKED;

struct gpu_resource_create_2d {
    struct gpu_ctrl_hdr hdr;
    u32 resource_id;
    u32 format;
    u32 width;
    u32 height;
} PACKED;

struct gpu_resource_unref {
    struct gpu_ctrl_hdr hdr;
    u32 resource_id;
    u32 padding;
} PACKED;

struct gpu_set_scanout {
    struct gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    u32 scanout_id;
    u32 resource_id;
} PACKED;

struct gpu_resource_flush {
    struct gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    u32 resource_id;
    u32 padding;
} PACKED;

struct gpu_transfer_to_host_2d {
    struct gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    u64 offset;
    u32 resource_id;
    u32 padding;
} PACKED;

struct gpu_mem_entry {
    u64 addr;
    u32 length;
    u32 padding;
} PACKED;

/* The driver submits one command at a time. Keeping this 16 KiB coalescing
 * workspace out of the boot/task stack avoids overflowing the small kernel
 * stacks during framebuffer attachment. */
static struct gpu_mem_entry backing_runs[1024];

struct gpu_attach_backing_hdr {
    struct gpu_ctrl_hdr hdr;
    u32 resource_id;
    u32 nr_entries;
} PACKED;

static int submit_two_buf(u32 req_len, u32 resp_len) {
    u16 d0 = virtq_alloc_desc(&controlq);
    u16 d1 = virtq_alloc_desc(&controlq);
    if (d0 == 0xFFFF || d1 == 0xFFFF) {
        if (d0 != 0xFFFF)
            virtq_free_desc(&controlq, d0);
        if (d1 != 0xFFFF)
            virtq_free_desc(&controlq, d1);
        log_write("gpu: no free descs", KERNEL, LOG_ERROR);
        return -1;
    }

    controlq.desc[d0].addr  = scratch_req_phys;
    controlq.desc[d0].len   = req_len;
    controlq.desc[d0].flags = VIRTQ_DESC_F_NEXT;
    controlq.desc[d0].next  = d1;

    controlq.desc[d1].addr  = scratch_resp_phys;
    controlq.desc[d1].len   = resp_len;
    controlq.desc[d1].flags = VIRTQ_DESC_F_WRITE;
    controlq.desc[d1].next  = 0;

    virtq_submit(&controlq, d0);
    virtio_queue_notify(&vdev, &controlq);

    u16 got_id = 0;
    u32 got_len = 0;

    /* Busy-wait for 1,000,000 iterations. QEMU processes virtio-gpu commands
     * almost instantly on an unloaded VM, so this loop usually exits in
     * under a microsecond. No sleeps, no yields, no 10ms delays! */
    for (u32 i = 0; i < 1000000; i++) {
        if (virtq_reap(&controlq, &got_id, &got_len)) goto done;
        __asm__ volatile ("pause");
    }

    log_write("gpu: command timed out", KERNEL, LOG_ERROR);
    return -1;
done:
    virtq_free_desc(&controlq, d0);
    virtq_free_desc(&controlq, d1);
    return 0;
}

struct gpu_batch_command {
    u16 request_desc;
    u16 response_desc;
    u8 *response;
    int complete;
};

/* Add one request/response descriptor chain to controlq without ringing the
 * doorbell.  The caller submits all commands first, then kicks once. */
static int submit_batch_command(struct gpu_batch_command *cmd,
                                const struct gpu_batch_buffer *buf,
                                u32 req_len, u32 resp_len) {
    u16 d0 = virtq_alloc_desc(&controlq);
    u16 d1 = virtq_alloc_desc(&controlq);
    if (d0 == 0xFFFF || d1 == 0xFFFF) {
        if (d0 != 0xFFFF)
            virtq_free_desc(&controlq, d0);
        if (d1 != 0xFFFF)
            virtq_free_desc(&controlq, d1);
        log_write("gpu: no free batch descs", KERNEL, LOG_ERROR);
        return -1;
    }

    controlq.desc[d0].addr = buf->req_phys;
    controlq.desc[d0].len = req_len;
    controlq.desc[d0].flags = VIRTQ_DESC_F_NEXT;
    controlq.desc[d0].next = d1;

    controlq.desc[d1].addr = buf->resp_phys;
    controlq.desc[d1].len = resp_len;
    controlq.desc[d1].flags = VIRTQ_DESC_F_WRITE;
    controlq.desc[d1].next = 0;

    cmd->request_desc = d0;
    cmd->response_desc = d1;
    cmd->response = buf->resp;
    cmd->complete = 0;
    virtq_submit(&controlq, d0);
    return 0;
}

static void finish_batch_command(struct gpu_batch_command *cmd) {
    virtq_free_desc(&controlq, cmd->request_desc);
    virtq_free_desc(&controlq, cmd->response_desc);
    cmd->complete = 1;
}

/* Wait for every response in a submitted group.  We identify completion by
 * descriptor-chain head rather than assuming the device reports used entries
 * in submission order. */
static int wait_batch_commands(struct gpu_batch_command *cmds, u32 count) {
    u32 remaining = count;
    u32 budget = count * 1000000U;
    int failed = 0;
    while (remaining && budget--) {
        u16 got_id;
        u32 got_len;
        if (!virtq_reap(&controlq, &got_id, &got_len)) {
            __asm__ volatile ("pause");
            continue;
        }

        int found = -1;
        for (u32 i = 0; i < count; i++) {
            if (!cmds[i].complete && cmds[i].request_desc == got_id) {
                found = (int)i;
                break;
            }
        }
        if (found < 0) {
            log_write_hex("gpu: unexpected batch response =", got_id,
                          KERNEL, LOG_ERROR);
            failed = 1;
            continue;
        }

        struct gpu_ctrl_hdr *resp = (struct gpu_ctrl_hdr *)cmds[found].response;
        if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
            log_write_hex("gpu: batch command bad resp =", resp->type,
                          KERNEL, LOG_ERROR);
            failed = 1;
        }
        finish_batch_command(&cmds[found]);
        remaining--;
    }

    if (remaining) {
        /* This matches the existing synchronous timeout policy: do not wedge
         * the flush worker forever if a broken host never completes a chain. */
        log_write("gpu: batch command timed out", KERNEL, LOG_ERROR);
        for (u32 i = 0; i < count; i++) {
            if (!cmds[i].complete)
                finish_batch_command(&cmds[i]);
        }
        return -1;
    }
    return failed ? -1 : 0;
}

/* Issue a command whose request body lives in scratch_req and whose response
 * goes to scratch_resp. Returns response type, or 0 on failure. */
static u32 do_cmd(u32 req_len, u32 resp_len) {
    /* All request types begin with gpu_ctrl_hdr. Without this fence, the GL
     * backend may reply before a TRANSFER_TO_HOST_2D has reached the host
     * texture, allowing the following RESOURCE_FLUSH to present stale or
     * partially updated pixels. */
    struct gpu_ctrl_hdr *req = (struct gpu_ctrl_hdr*)scratch_req;
    req->flags |= VIRTIO_GPU_FLAG_FENCE;
    req->fence_id = ++gpu_fence_id;

    if (submit_two_buf(req_len, resp_len) != 0) return 0;
    struct gpu_ctrl_hdr *resp = (struct gpu_ctrl_hdr*)scratch_resp;
    return resp->type;
}

static int do_get_display_info(u32 *w, u32 *h) {
    memset(scratch_req,  0, sizeof(struct gpu_ctrl_hdr));
    memset(scratch_resp, 0, sizeof(struct gpu_resp_display_info));
    struct gpu_ctrl_hdr *h_req = (struct gpu_ctrl_hdr*)scratch_req;
    h_req->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    u32 resp_type = do_cmd(sizeof(*h_req), sizeof(struct gpu_resp_display_info));
    if (resp_type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
        log_write_hex("gpu: display_info bad resp =", resp_type, KERNEL, LOG_ERROR);
        return -1;
    }
    struct gpu_resp_display_info *r = (struct gpu_resp_display_info*)scratch_resp;
    /* Find the first enabled scanout. Most QEMU configs put it at index 0. */
    for (int i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
        if (r->pmodes[i].enabled) {
            *w = r->pmodes[i].r.width;
            *h = r->pmodes[i].r.height;
            return 0;
        }
    }
    log_write("gpu: no enabled scanouts", KERNEL, LOG_ERROR);
    return -1;
}

static int do_resource_create_2d(u32 rid, u32 format,
                                 u32 w, u32 h) {
    memset(scratch_req,  0, sizeof(struct gpu_resource_create_2d));
    memset(scratch_resp, 0, sizeof(struct gpu_ctrl_hdr));
    struct gpu_resource_create_2d *q = (struct gpu_resource_create_2d*)scratch_req;
    q->hdr.type    = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    q->resource_id = rid;
    q->format      = format;
    q->width       = w;
    q->height      = h;
    u32 t = do_cmd(sizeof(*q), sizeof(struct gpu_ctrl_hdr));
    if (t != VIRTIO_GPU_RESP_OK_NODATA) {
        log_write_hex("gpu: create_2d bad resp =", t, KERNEL, LOG_ERROR);
        return -1;
    }
    return 0;
}

static int do_resource_unref(u32 rid) {
    memset(scratch_req,  0, sizeof(struct gpu_resource_unref));
    memset(scratch_resp, 0, sizeof(struct gpu_ctrl_hdr));
    struct gpu_resource_unref *q = (struct gpu_resource_unref*)scratch_req;
    q->hdr.type    = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    q->resource_id = rid;
    u32 t = do_cmd(sizeof(*q), sizeof(struct gpu_ctrl_hdr));
    if (t != VIRTIO_GPU_RESP_OK_NODATA) {
        log_write_hex("gpu: unref bad resp =", t, KERNEL, LOG_ERROR);
        return -1;
    }
    return 0;
}

static int do_attach_backing(u32 rid,
                             const u64 *page_phys, u32 n_pages) {
    /* Layout: [hdr][nr_entries x mem_entry]. Whole thing into scratch_req. */
    const u32 max_entries =
        (4096 - sizeof(struct gpu_attach_backing_hdr)) /
        sizeof(struct gpu_mem_entry);

    /* Coalesce adjacent pages: contiguous runs of identity-mapped frames are
     * the common case (pmm allocs sequentially when memory is fresh), and the
     * spec allows arbitrary entry length. Coalescing keeps us under the entry
     * cap for large framebuffers. */
    u32 entries = 0;
    if (n_pages == 0) return -1;

    backing_runs[0].addr = page_phys[0];
    backing_runs[0].length = 4096;
    backing_runs[0].padding = 0;
    entries = 1;
    for (u32 i = 1; i < n_pages; i++) {
        if (page_phys[i] == backing_runs[entries - 1].addr
                          + backing_runs[entries - 1].length) {
            backing_runs[entries - 1].length += 4096;
        } else {
            if (entries >= 1024) {
                log_write("gpu: too many backing runs", KERNEL, LOG_ERROR);
                return -1;
            }
            backing_runs[entries].addr    = page_phys[i];
            backing_runs[entries].length  = 4096;
            backing_runs[entries].padding = 0;
            entries++;
        }
    }

    if (entries > max_entries) {
        log_write_hex("gpu: backing entries overflow =", entries, KERNEL, LOG_ERROR);
        return -1;
    }

    struct gpu_attach_backing_hdr *q = (struct gpu_attach_backing_hdr*)scratch_req;
    memset(q, 0, sizeof(*q));
    q->hdr.type    = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    q->resource_id = rid;
    q->nr_entries  = entries;

    struct gpu_mem_entry *tail = (struct gpu_mem_entry*)(scratch_req + sizeof(*q));
    for (u32 i = 0; i < entries; i++) tail[i] = backing_runs[i];

    u32 req_len = (u32)sizeof(*q) + entries * (u32)sizeof(struct gpu_mem_entry);
    memset(scratch_resp, 0, sizeof(struct gpu_ctrl_hdr));
    u32 t = do_cmd(req_len, sizeof(struct gpu_ctrl_hdr));
    if (t != VIRTIO_GPU_RESP_OK_NODATA) {
        log_write_hex("gpu: attach_backing bad resp =", t, KERNEL, LOG_ERROR);
        return -1;
    }
    return 0;
}

static int do_set_scanout(u32 scanout_id, u32 rid,
                          u32 w, u32 h) {
    memset(scratch_req,  0, sizeof(struct gpu_set_scanout));
    memset(scratch_resp, 0, sizeof(struct gpu_ctrl_hdr));
    struct gpu_set_scanout *q = (struct gpu_set_scanout*)scratch_req;
    q->hdr.type    = VIRTIO_GPU_CMD_SET_SCANOUT;
    q->r.x         = 0;
    q->r.y         = 0;
    q->r.width     = w;
    q->r.height    = h;
    q->scanout_id  = scanout_id;
    q->resource_id = rid;
    u32 t = do_cmd(sizeof(*q), sizeof(struct gpu_ctrl_hdr));
    if (t != VIRTIO_GPU_RESP_OK_NODATA) {
        log_write_hex("gpu: set_scanout bad resp =", t, KERNEL, LOG_ERROR);
        return -1;
    }
    return 0;
}

int virtio_gpu_init(void) {
    struct pci_device dev;
    if (!pci_find_by_id(VIRTIO_PCI_VENDOR, VIRTIO_GPU_DEVICE_ID, &dev)) {
        log_write("gpu: virtio-gpu PCI device not found", KERNEL, LOG_ERROR);
        return -1;
    }

    if (virtio_pci_init(&dev, &vdev) != 0) return -1;

    /* No optional features required for our minimal usage. Negotiate empty
     * set (just VERSION_1, which virtio_negotiate ORs in unconditionally). */
    if (virtio_negotiate(&vdev, 0) != 0) return -1;

    /* We do not ack VIRGL, but its presence in the offered set identifies the
     * host as a GL backend, which presents scanouts differently. */
    gpu_is_gl_backend = (vdev.device_features & VIRTIO_GPU_F_VIRGL) != 0;
    log_write_hex("gpu: gl backend =", (u64)gpu_is_gl_backend, KERNEL, LOG_INFO);

    if (virtio_queue_setup(&vdev, 0, &controlq) != 0) return -1;
    /* Queue 1 (cursorq) is optional , we leave it unconfigured. */
    virtio_queue_enable(&vdev, &controlq);

    /* The queue is now fully described to the device.  Publish DRIVER_OK
     * before issuing GET_DISPLAY_INFO: the virtio 1.x lifecycle permits the
     * device to start consuming queue traffic only after this transition.
     * QEMU's plain 2D renderer happened to accept early commands, whereas
     * accelerated backends can initialise lazily on the first request. */
    virtio_driver_ok(&vdev);

    /* Allocate physical scratch pages and access them through the HHDM. */
    scratch_req_phys  = pmm_alloc_frame();
    scratch_resp_phys = pmm_alloc_frame();
    if (!scratch_req_phys || !scratch_resp_phys) {
        log_write("gpu: scratch alloc failed", KERNEL, LOG_ERROR);
        return -1;
    }
    scratch_req  = phys_to_virt(scratch_req_phys);
    scratch_resp = phys_to_virt(scratch_resp_phys);

    /* Failure here is non-fatal: the old synchronous flush path is still a
     * correct (if slower) way to drive the device. */
    gpu_batch_ready = 1;
    for (u32 i = 0; i < GPU_BATCH_COMMANDS; i++) {
        gpu_batch_buffers[i].req_phys = pmm_alloc_frame();
        gpu_batch_buffers[i].resp_phys = pmm_alloc_frame();
        if (!gpu_batch_buffers[i].req_phys || !gpu_batch_buffers[i].resp_phys) {
            gpu_batch_ready = 0;
            log_write("gpu: batch buffers unavailable; using sync flush",
                      KERNEL, LOG_WARN);
            break;
        }
        gpu_batch_buffers[i].req = phys_to_virt(gpu_batch_buffers[i].req_phys);
        gpu_batch_buffers[i].resp = phys_to_virt(gpu_batch_buffers[i].resp_phys);
    }

    virtio_driver_ok(&vdev);

    u32 w = 0, h = 0;
    if (do_get_display_info(&w, &h) != 0) return -1;
    gpu_state.scanout_w = w;
    gpu_state.scanout_h = h;
    gpu_state.resource_id = 0;
    gpu_state.resource_w = 0;
    gpu_state.resource_h = 0;
    gpu_state.ready = 1;
    log_write_hex("gpu: scanout w =", w, KERNEL, LOG_INFO);
    log_write_hex("gpu: scanout h =", h, KERNEL, LOG_INFO);
    return 0;
}

int virtio_gpu_get_dims(u32 *w, u32 *h) {
    if (!gpu_state.ready) return -1;
    *w = gpu_state.scanout_w;
    *h = gpu_state.scanout_h;
    return 0;
}

int virtio_gpu_create_scanout_2d(u32 resource_w, u32 resource_h,
                                 u32 scanout_w, u32 scanout_h,
                                 const u64 *page_phys, u32 n_pages) {
    if (!gpu_state.ready) return -1;
    if (resource_w == 0 || resource_h == 0 || scanout_w == 0 || scanout_h == 0)
        return -1;
    if (scanout_w > resource_w || scanout_h > resource_h) return -1;
    if ((u64)n_pages * 4096 <
        (u64)resource_w * (u64)resource_h * 4) return -1;

    /* Tear down the previous resource if any. SET_SCANOUT with resource_id=0
     * detaches the scanout cleanly per spec; UNREF then drops the resource. */
    if (gpu_state.resource_id) {
        do_set_scanout(0, 0, 0, 0);
        do_resource_unref(gpu_state.resource_id);
        gpu_state.resource_id = 0;
        gpu_state.resource_w = 0;
        gpu_state.resource_h = 0;
    }

    u32 rid = 1;   /* virtio-gpu resource IDs are driver-assigned; 1 is fine. */
    if (do_resource_create_2d(rid, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM,
                              resource_w, resource_h) != 0) return -1;
    if (do_attach_backing(rid, page_phys, n_pages) != 0) {
        do_resource_unref(rid);
        return -1;
    }
    if (do_set_scanout(0, rid, scanout_w, scanout_h) != 0) {
        do_resource_unref(rid);
        return -1;
    }

    gpu_state.resource_id = rid;
    gpu_state.resource_w  = resource_w;
    gpu_state.resource_h  = resource_h;
    gpu_state.scanout_w   = scanout_w;
    gpu_state.scanout_h   = scanout_h;
    return 0;
}

int virtio_gpu_resize_scanout_2d(u32 w, u32 h) {
    if (!gpu_state.ready || !gpu_state.resource_id) return -1;
    if (w == 0 || h == 0 || w > gpu_state.resource_w || h > gpu_state.resource_h)
        return -1;
    if (do_set_scanout(0, gpu_state.resource_id, w, h) != 0) return -1;
    gpu_state.scanout_w = w;
    gpu_state.scanout_h = h;
    return 0;
}

static int virtio_gpu_flush_rect_sync(u32 x, u32 y, u32 w, u32 h) {
    if (!gpu_state.ready || !gpu_state.resource_id) return -1;

    /* TRANSFER_TO_HOST_2D: copy guest-side pixels into the host resource. */
    {
        memset(scratch_req,  0, sizeof(struct gpu_transfer_to_host_2d));
        memset(scratch_resp, 0, sizeof(struct gpu_ctrl_hdr));
        struct gpu_transfer_to_host_2d *q = (struct gpu_transfer_to_host_2d*)scratch_req;
        q->hdr.type    = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
        q->r.x         = x;
        q->r.y         = y;
        q->r.width     = w;
        q->r.height    = h;
        q->offset      = (u64)y * (u64)gpu_state.resource_w * 4 +
                         (u64)x * 4;
        q->resource_id = gpu_state.resource_id;
        u32 t = do_cmd(sizeof(*q), sizeof(struct gpu_ctrl_hdr));
        if (t != VIRTIO_GPU_RESP_OK_NODATA) {
            log_write_hex("gpu: xfer2d bad resp =", t, KERNEL, LOG_ERROR);
            return -1;
        }
    }
    /* RESOURCE_FLUSH: tell the host to actually show what we just transferred. */
    {
        memset(scratch_req,  0, sizeof(struct gpu_resource_flush));
        memset(scratch_resp, 0, sizeof(struct gpu_ctrl_hdr));
        struct gpu_resource_flush *q = (struct gpu_resource_flush*)scratch_req;
        q->hdr.type    = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
        q->r.x         = x;
        q->r.y         = y;
        q->r.width     = w;
        q->r.height    = h;
        q->resource_id = gpu_state.resource_id;
        u32 t = do_cmd(sizeof(*q), sizeof(struct gpu_ctrl_hdr));
        if (t != VIRTIO_GPU_RESP_OK_NODATA) {
            log_write_hex("gpu: flush bad resp =", t, KERNEL, LOG_ERROR);
            return -1;
        }
    }
    return 0;
}

int virtio_gpu_flush_rects(const struct virtio_gpu_rect *rects,
                           u32 rect_count) {
    if (!gpu_state.ready || !gpu_state.resource_id || !rects ||
        rect_count == 0 || rect_count > VIRTIO_GPU_MAX_FLUSH_RECTS)
        return -1;

    for (u32 i = 0; i < rect_count; i++) {
        if (rects[i].width == 0 || rects[i].height == 0)
            return -1;
    }

    if (!gpu_batch_ready) {
        for (u32 i = 0; i < rect_count; i++) {
            if (virtio_gpu_flush_rect_sync(rects[i].x, rects[i].y,
                                           rects[i].width, rects[i].height) != 0)
                return -1;
        }
        return 0;
    }

    u32 first = 0;
    while (first < rect_count) {
        /* Each region needs two chains of two descriptors.  Respect smaller
         * device queues by splitting only as far as necessary. */
        u32 max_regions = controlq.num_free / 4;
        if (max_regions == 0) {
            log_write("gpu: controlq has no batch capacity", KERNEL, LOG_ERROR);
            return -1;
        }
        u32 count = rect_count - first;
        if (count > max_regions)
            count = max_regions;

        struct gpu_batch_command cmds[GPU_BATCH_COMMANDS];
        u32 command_count = 0;
        int submit_failed = 0;

        /* Queue every transfer before every flush.  A single fence on the
         * last flush orders the entire group on accelerated host renderers. */
        for (u32 i = 0; i < count; i++) {
            const struct virtio_gpu_rect *r = &rects[first + i];
            struct gpu_batch_buffer *buf = &gpu_batch_buffers[command_count];
            memset(buf->req, 0, sizeof(struct gpu_transfer_to_host_2d));
            memset(buf->resp, 0, sizeof(struct gpu_ctrl_hdr));
            struct gpu_transfer_to_host_2d *q =
                (struct gpu_transfer_to_host_2d *)buf->req;
            q->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
            q->r = *r;
            q->offset = (u64)r->y * (u64)gpu_state.resource_w * 4 +
                        (u64)r->x * 4;
            q->resource_id = gpu_state.resource_id;
            if (submit_batch_command(&cmds[command_count], buf, sizeof(*q),
                                     sizeof(struct gpu_ctrl_hdr)) != 0) {
                submit_failed = 1;
                break;
            }
            command_count++;
        }
        for (u32 i = 0; !submit_failed && i < count; i++) {
            const struct virtio_gpu_rect *r = &rects[first + i];
            struct gpu_batch_buffer *buf = &gpu_batch_buffers[command_count];
            memset(buf->req, 0, sizeof(struct gpu_resource_flush));
            memset(buf->resp, 0, sizeof(struct gpu_ctrl_hdr));
            struct gpu_resource_flush *q = (struct gpu_resource_flush *)buf->req;
            q->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
            q->r = *r;
            q->resource_id = gpu_state.resource_id;
            if (i + 1 == count) {
                q->hdr.flags = VIRTIO_GPU_FLAG_FENCE;
                q->hdr.fence_id = ++gpu_fence_id;
            }
            if (submit_batch_command(&cmds[command_count], buf, sizeof(*q),
                                     sizeof(struct gpu_ctrl_hdr)) != 0) {
                submit_failed = 1;
                break;
            }
            command_count++;
        }

        if (command_count) {
            virtio_queue_notify(&vdev, &controlq);
            if (wait_batch_commands(cmds, command_count) != 0)
                return -1;
        }
        if (submit_failed)
            return -1;
        first += count;
    }
    return 0;
}

int virtio_gpu_flush_rect(u32 x, u32 y, u32 w, u32 h) {
    const struct virtio_gpu_rect rect = {
        .x = x, .y = y, .width = w, .height = h,
    };
    return virtio_gpu_flush_rects(&rect, 1);
}

int virtio_gpu_scanout_needs_exact_resource(void) {
    return gpu_is_gl_backend;
}

int virtio_gpu_poll_display_event(void) {
    if (!gpu_state.ready) return 0;
    volatile struct virtio_gpu_config *cfg =
        (volatile struct virtio_gpu_config*)vdev.device_cfg;
    u32 ev = cfg->events_read;
    if (!(ev & VIRTIO_GPU_EVENT_DISPLAY)) return 0;
    /* Ack: write the same bits to events_clear. */
    cfg->events_clear = VIRTIO_GPU_EVENT_DISPLAY;

    /* Re-read display info so subsequent virtio_gpu_get_dims reflects the
     * new size. The actual scanout still has the old resource attached;
     * caller is expected to resize its visible rectangle. */
    u32 w = 0, h = 0;
    if (do_get_display_info(&w, &h) == 0) {
        gpu_state.scanout_w = w;
        gpu_state.scanout_h = h;
    }
    return 1;
}
