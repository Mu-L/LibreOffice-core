# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
nib_WRKDIR := $(gb_CustomTarget_workdir)/vcl/osx/res

$(nib_WRKDIR)/.dir:
	mkdir -p $(@D) && touch $@

$(nib_WRKDIR)/MainMenu.nib: $(SRCDIR)/vcl/osx/res/MainMenu.xib | $(nib_WRKDIR)/.dir
	$(call gb_Output_announce,$(@F),$(true),NIB,2)
	ibtool --compile "$@" "$<"

$(eval $(call gb_Package_Package,vcl_osxres,$(nib_WRKDIR)))
$(call gb_Package_get_clean_target,vcl_osxres): $(call gb_CustomTarget_get_clean_target,vcl/osx/res)

$(eval $(call gb_Package_add_files_with_dir,vcl_osxres,Resources,\
	MainMenu.nib \
))

# vim:set noet sw=4 ts=4:
