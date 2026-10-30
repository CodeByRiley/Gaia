/* <gaia/audio.h> , PCM output.
 *
 * One process owns the output device at a time: audio_open() claims it and
 * audio_close() releases it. Samples are frame-aligned signed 16-bit
 * little-endian stereo (AUDIO_FORMAT_S16_LE, AUDIO_CHANNELS_STEREO).
 */
#ifndef GAIA_SDK_AUDIO_H
#define GAIA_SDK_AUDIO_H

#include <gaia/abi.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_CHANNELS_STEREO 2

#define AUDIO_ERR_NO_DEVICE (-1)
#define AUDIO_ERR_BUSY (-2)
#define AUDIO_ERR_INVALID (-3)
#define AUDIO_ERR_NOT_OWNER (-4)

long audio_open(uint32_t sample_rate, uint32_t channels, uint32_t format);
/* Non-blocking: queues what fits and returns the byte count, which may be
 * short or zero when the ring is full. */
long audio_write(const void *pcm, size_t bytes);
long audio_status(struct audio_status *out);
/* Block until everything queued has played. */
long audio_drain(void);
/* Release the device, discarding anything still queued. */
long audio_close(void);
long audio_set_volume(int percent);
long audio_pause(void);
long audio_resume(void);

#ifdef __cplusplus
}
#endif

#endif
