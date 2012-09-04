#ifndef __version_h__
#define __version_h__

#include "helpers.h"

#define RHI_VERSION_MAJOR 0
#define RHI_VERSION_MINOR 1
#define RHI_VERSION_PATCH 1
#define RHI_VERSION_FULL    STRINGIFY(RHI_VERSION_MAJOR) "." \
                            STRINGIFY(RHI_VERSION_MINOR) "." \
                            STRINGIFY(RHI_VERSION_PATCH)

#define RHI_NAME "Rhizofs"
#define RHI_NAME_LOWER "rhizofs"

#endif /* __version_h__ */
