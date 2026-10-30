# Expand each manifest in isolation, capture values, then emit concrete rules.
APP_MANIFESTS := $(sort $(wildcard bin/*/build.mk))
APP_NAMES :=
APP_DEP_FILES :=
app-name-chars := a b c d e f g h i j k l m n o p q r s t u v w x y z A B C D E F G H I J K L M N O P Q R S T U V W X Y Z 0 1 2 3 4 5 6 7 8 9 _ -
remove-chars = $(if $(2),$(call remove-chars,$(subst $(firstword $(2)),,$(1)),$(wordlist 2,99,$(2))),$(1))

define newline


endef

define app-object
$(1): $(2) $(3)
	@mkdir -p $$(dir $$@)
	$(if $(filter pe.%,$(4)),$(USER_PE_CC),$(USER_ELF_CC)) $(RUNTIME_$(4)_CFLAGS) $(5) -MMD -MP -c $(2) -o $$@
endef

# NASM uses no -c; emit assembly separately to keep compiler syntax independent.
define app-asm
$(1): $(2) $(3)
	@mkdir -p $$(dir $$@)
	$(USER_AS) -f $(if $(filter pe,$(4)),win64,elf64) $$< -o $$@
endef

define app-link
$(1): $(2) $(3) $(4) build/application.mk build/runtimes.mk build/toolchains.mk
	$(if $(filter elf.musl,$(5)),CC=$(USER_ELF_CC) "$$(SHELL)" $(MUSL_LINK) $$@ $(2) $(3) $(6),$(if $(filter pe.native,$(5)),$(USER_PE_CC) $(PE_LDFLAGS),$(USER_ELF_LD) $(USER_LDFLAGS)) $(6) $(2) $(3) -o $$@)
endef

define register-format
$(eval _runtime := $(1).$(if $(filter elf,$(1)),$(APP_ELF_RUNTIME),$(APP_PE_RUNTIME)))
$(if $(filter $(_runtime),$(SUPPORTED_RUNTIMES)),,$(error $(2): unsupported runtime '$(_runtime)'))
$(if $(filter-out $(RUNTIME_$(_runtime)_LIBS),$(APP_LIBS)),$(error $(2): unknown libraries '$(APP_LIBS)' for $(_runtime)))
$(eval _libs := $(foreach l,$(APP_LIBS),$(RUNTIME_$(_runtime)_$(l))))
$(eval _objects := $(foreach src,$(APP_SOURCES),../build/userspace/$(1)/$(APP_NAME)/$(src).o))
$(eval _output := $(_dir)/$(APP_NAME).$(if $(filter elf,$(1)),elf,exe))
$(foreach src,$(APP_SOURCES),$(if $(filter .s .asm,$(suffix $(src))),$(eval $(call app-asm,../build/userspace/$(1)/$(APP_NAME)/$(src).o,$(_dir)/$(src),$(2),$(1))),$(eval $(call app-object,../build/userspace/$(1)/$(APP_NAME)/$(src).o,$(_dir)/$(src),$(2) Makefile build/application.mk build/toolchains.mk build/runtimes.mk $(RUNTIME_$(_runtime)_DEPS),$(_runtime),$(APP_CFLAGS)))))
$(eval $(call app-link,$(_output),$(_objects),$(_libs),$(2) $(RUNTIME_$(_runtime)_DEPS),$(_runtime),$(APP_LDFLAGS)))
$(eval USERSPACE_TARGETS += $(_output))
$(eval APP_DEP_FILES += $(_objects:.o=.d))
endef

# undefine matters: an explicit empty APP_LIBS means a self-contained program.
define register-application
$(foreach v,NAME FORMAT RUNTIME ELF_RUNTIME PE_RUNTIME SOURCES LIBS CFLAGS LDFLAGS,$(eval undefine APP_$(v)))
$(eval _dir := $(patsubst %/,%,$(dir $(1))))
$(eval APP_NAME := $(notdir $(_dir)))
$(eval APP_FORMAT := elf)
$(eval include $(1))
$(eval APP_RUNTIME ?= $(if $(filter pe,$(APP_FORMAT)),native,musl))
$(if $(and $(APP_NAME),$(filter 1,$(words $(APP_NAME))),$(if $(call remove-chars,$(APP_NAME),$(app-name-chars)),,yes),$(if $(filter -%,$(APP_NAME)),,yes)),,$(error $(1): invalid APP_NAME '$(APP_NAME)'))
$(if $(filter $(APP_NAME),$(APP_NAMES)),$(error $(1): duplicate APP_NAME '$(APP_NAME)'))
$(eval APP_NAMES += $(APP_NAME))
$(if $(and $(filter $(APP_FORMAT),elf pe both),$(filter 1,$(words $(APP_FORMAT)))),,$(error $(1): APP_FORMAT must be elf, pe, or both))
$(eval APP_ELF_RUNTIME ?= $(APP_RUNTIME))
$(eval APP_PE_RUNTIME ?= $(if $(filter pe,$(APP_FORMAT)),$(APP_RUNTIME),native))
$(if $(APP_SOURCES),,$(error $(1): APP_SOURCES is required))
$(if $(filter-out %.c %.s %.S %.asm,$(APP_SOURCES)),$(error $(1): unsupported source extension))
$(foreach src,$(APP_SOURCES),$(if $(or $(filter /% %:,$(src)),$(findstring :,$(src)),$(findstring ..,$(src)),$(findstring \,$(src))),$(error $(1): source must be a local relative path: $(src)))$(if $(wildcard $(_dir)/$(src)),,$(error $(1): missing source $(src))))
$(if $(filter-out musl native,$(APP_RUNTIME)),$(error $(1): APP_RUNTIME must be musl or native))
$(if $(filter undefined,$(origin APP_LIBS)),$(eval APP_LIBS := gaia))
$(foreach fmt,$(if $(filter both,$(APP_FORMAT)),elf pe,$(APP_FORMAT)),$(call register-format,$(fmt),$(1)))
endef

$(foreach manifest,$(APP_MANIFESTS),$(call register-application,$(manifest)))
-include $(APP_DEP_FILES)
