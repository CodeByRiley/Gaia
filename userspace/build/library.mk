# Library manifests are read first, then dependencies and rules are resolved.
LIB_MANIFESTS := $(sort $(wildcard shims/*/*/build.mk shims/*/*/*/build.mk))
ifeq ($(BUILD_COMPAT_TESTS),0)
LIB_MANIFESTS := $(filter-out shims/win32/compatdep/% shims/win32/compatprobe/%,$(LIB_MANIFESTS))
endif
LIB_NAMES :=
SHIM_TARGETS :=
SHIM_DEP_FILES :=

define read-library
$(foreach v,NAME TYPE FORMAT ABI SOURCES DEPS CFLAGS LDFLAGS EXPORTS ENTRY,$(eval undefine LIB_$(v)))
$(eval LIB_TYPE := static)
$(eval LIB_FORMAT := elf)
$(eval LIB_ENTRY := 0)
$(eval include $(1))
$(if $(and $(LIB_NAME),$(if $(call remove-chars,$(LIB_NAME),$(app-name-chars)),,yes),$(filter 1,$(words $(LIB_NAME)))),,$(error $(1): invalid LIB_NAME))
$(if $(filter $(LIB_NAME),$(LIB_NAMES)),$(error $(1): duplicate LIB_NAME))
$(if $(filter $(LIB_TYPE),static shared),,$(error $(1): LIB_TYPE must be static or shared))
$(if $(filter $(LIB_FORMAT).$(LIB_ABI).$(LIB_TYPE),elf.posix.static pe.win32.static pe.win32.shared),,$(error $(1): unsupported library format/ABI/type))
$(if $(or $(LIB_SOURCES),$(and $(filter shared,$(LIB_TYPE)),$(LIB_EXPORTS))),,$(error $(1): library needs sources or exports))
$(eval LIB_NAMES += $(LIB_NAME))
$(eval lib.$(LIB_NAME).manifest := $(1))
$(foreach v,TYPE FORMAT ABI DEPS CFLAGS LDFLAGS ENTRY,$(eval lib.$(LIB_NAME).$(v) := $(LIB_$(v))))
$(eval lib.$(LIB_NAME).SOURCES := $(foreach s,$(LIB_SOURCES),$(patsubst $(CURDIR)/%,%,$(abspath $(dir $(1))/$(s)))))
$(foreach s,$(lib.$(LIB_NAME).SOURCES),$(if $(or $(filter /%,$(s)),$(findstring :,$(s)),$(filter-out %.c %.S,$(s))),$(error $(1): source outside userspace or unsupported extension: $(s)))$(if $(wildcard $(s)),,$(error $(1): missing source $(s))))
$(eval lib.$(LIB_NAME).EXPORTS := $(if $(LIB_EXPORTS),$(dir $(1))$(LIB_EXPORTS)))
$(if $(LIB_EXPORTS),$(if $(wildcard $(lib.$(LIB_NAME).EXPORTS)),,$(error $(1): missing export definition)))
$(eval lib.$(LIB_NAME).TARGET := ../build/userspace/$(LIB_FORMAT)/shims/$(LIB_NAME).$(if $(filter shared,$(LIB_TYPE)),dll,a))
$(eval lib.$(LIB_NAME).LINK := $(if $(filter shared,$(LIB_TYPE)),../build/userspace/$(LIB_FORMAT)/shims/$(LIB_NAME).dll.a,$(lib.$(LIB_NAME).TARGET)))
endef
$(foreach m,$(LIB_MANIFESTS),$(call read-library,$(m)))

check-library = $(if $(filter $(1),$(2)),$(error cyclic library dependency: $(2) $(1)),$(foreach d,$(lib.$(1).DEPS),$(if $(filter $(d),$(LIB_NAMES)),$(if $(filter $(lib.$(1).FORMAT),$(lib.$(d).FORMAT)),,$(error $(1): dependency $(d) has incompatible object format))$(call check-library,$(d),$(2) $(1)),$(error $(1): unknown library dependency $(d)))))
$(foreach l,$(LIB_NAMES),$(call check-library,$(l),))

define library-object
$(1): $(2) $(3) build/library.mk build/toolchains.mk
	@mkdir -p $$(dir $$@)
	$(if $(filter pe,$(4)),$(USER_PE_CC) $(PE_CFLAGS),$(USER_ELF_CC) $(USER_CFLAGS)) -fno-builtin -fno-stack-protector -I include/shims/win32 $(5) -MMD -MP -c $$< -o $$@
endef

define library-link
$(1) $(if $(filter shared,$(2)),$(3)) &: $(4) $(5) $(6) $(7) build/library.mk
	@mkdir -p $$(dir $$@)
	$(if $(filter static,$(2)),rm -f $(1) && )$(if $(filter shared,$(2)),$(USER_PE_CC) -shared -nostdlib -static -Wl$(comma)--entry$(comma)$(8) -Wl$(comma)--out-implib$(comma)$(3) -Wl$(comma)--enable-auto-image-base $(4) $(6) $(9) -o $(1),$(if $(filter pe,$(10)),$(USER_PE_AR),x86_64-elf-ar) rcs $(1) $(4))
endef
comma := ,

define emit-library
$(eval _objects := $(foreach s,$(lib.$(1).SOURCES),../build/userspace/$(lib.$(1).FORMAT)/shims/$(1)/$(s).o))
$(foreach s,$(lib.$(1).SOURCES),$(eval $(call library-object,../build/userspace/$(lib.$(1).FORMAT)/shims/$(1)/$(s).o,$(s),$(lib.$(1).manifest),$(lib.$(1).FORMAT),$(lib.$(1).CFLAGS))))
$(eval $(call library-link,$(lib.$(1).TARGET),$(lib.$(1).TYPE),$(lib.$(1).LINK),$(_objects),$(foreach d,$(lib.$(1).DEPS),$(lib.$(d).TARGET) $(lib.$(d).LINK)),$(lib.$(1).EXPORTS),$(lib.$(1).manifest),$(lib.$(1).ENTRY),$(lib.$(1).LDFLAGS) $(foreach d,$(lib.$(1).DEPS),$(lib.$(d).LINK)),$(lib.$(1).FORMAT)))
$(eval SHIM_TARGETS += $(lib.$(1).TARGET))
$(eval SHIM_DEP_FILES += $(_objects:.o=.d))
endef
$(foreach l,$(LIB_NAMES),$(call emit-library,$(l)))
-include $(SHIM_DEP_FILES)
SHIM_DLLS := $(foreach l,$(LIB_NAMES),$(if $(filter shared,$(lib.$(l).TYPE)),$(lib.$(l).TARGET)))
