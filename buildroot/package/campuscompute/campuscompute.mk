################################################################################
# campuscompute - Shire Shell + CampusCompute (built from the local tree)
#
# NOTE: this package definition has NOT been built against a real Buildroot
# tree yet (a full Buildroot build was outside the scope of the environment in
# which it was written). The application itself builds with plain `make` on
# any Linux and does not need Buildroot.
################################################################################

CAMPUSCOMPUTE_VERSION = 1.0.0
CAMPUSCOMPUTE_SITE = $(BR2_EXTERNAL_SHIRE_PATH)/../campuscompute
CAMPUSCOMPUTE_SITE_METHOD = local

define CAMPUSCOMPUTE_BUILD_CMDS
	$(MAKE) -C $(@D) CC="$(TARGET_CC)" \
		CFLAGS="-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Iinclude $(TARGET_CFLAGS) $(TARGET_LDFLAGS)" \
		shire
endef

define CAMPUSCOMPUTE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/shire $(TARGET_DIR)/usr/bin/shire
	$(INSTALL) -d $(TARGET_DIR)/usr/share/campuscompute
	$(INSTALL) -m 0644 $(@D)/tests/banker_*.txt $(TARGET_DIR)/usr/share/campuscompute/
	grep -qxF /usr/bin/shire $(TARGET_DIR)/etc/shells 2>/dev/null || \
		echo /usr/bin/shire >> $(TARGET_DIR)/etc/shells
endef

$(eval $(generic-package))
