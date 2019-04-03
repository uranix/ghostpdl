#  Copyright (C) 2001-2019 Artifex Software, Inc.
#  All Rights Reserved.
#
#  This software is provided AS-IS with no warranty, either express or
#  implied.
#
#  This software is distributed under license and may not be copied, modified
#  or distributed except as expressly authorized under the terms of that
#  license.  Refer to licensing information at http://www.artifex.com/
#  or contact Artifex Software, Inc.,  1305 Grant Avenue - Suite 200,
#  Novato, CA 94945, U.S.A., +1(415)492-9861, for further information.
#
# Makefile fragment containing the current revision identification.

# Major and minor version numbers.
# MINOR0 is different from MINOR only if MINOR is a single digit.
GS_VERSION_MAJOR=9
GS_VERSION_MINOR=27
GS_VERSION_MINOR0=27
# Revision date: year x 10000 + month x 100 + day.
GS_REVISIONDATE=20190404
# Derived values
GS_VERSION=$(GS_VERSION_MAJOR)$(GS_VERSION_MINOR0)
GS_DOT_VERSION=$(GS_VERSION_MAJOR).$(GS_VERSION_MINOR0)
GS_REVISION=$(GS_VERSION)
