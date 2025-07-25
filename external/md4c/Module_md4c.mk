# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t; fill-column: 100 -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
$(eval $(call gb_Module_Module,md4c))
$(eval $(call gb_Module_add_targets,md4c,\
    UnpackedTarball_md4c \
    StaticLibrary_md4c \
))
# vim: set noet sw=4 ts=4:
