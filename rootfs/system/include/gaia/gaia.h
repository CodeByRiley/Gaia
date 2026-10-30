/* <gaia/gaia.h> , every public libgaia header.
 *
 * Link with -lgaia. musl supplies the C and POSIX library; libgaia adds the
 * Gaia-native services declared here and defines none of musl's symbols.
 *
 * Windowing, graphics, and input are not part of the SDK yet: their headers
 * still depend on in-tree paths.
 */
#ifndef GAIA_SDK_GAIA_H
#define GAIA_SDK_GAIA_H

#include <gaia/abi.h>
#include <gaia/audio.h>
#include <gaia/console.h>
#include <gaia/ipc.h>
#include <gaia/process.h>
#include <gaia/system.h>

#endif
