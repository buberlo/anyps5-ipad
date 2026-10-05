# Capability check against AnyPS5's Vulkan requirements.
# Needs a Vulkan loader and headers (pkg-config vulkan).

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror
CPPFLAGS += $(shell pkg-config --cflags vulkan)
LDLIBS += $(shell pkg-config --libs vulkan)

.PHONY: all clean
all: vk-requirements

vk-requirements: vk_requirements.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ vk_requirements.c $(LDLIBS)

clean:
	rm -f vk-requirements
