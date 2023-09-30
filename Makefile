include_lib := -IC:/mingw/projects/lib
library_lib := -LC:/mingw/projects/lib

INCLUDES := $(include_lib)/SDL2-2.28.0/include
LIBRARY_PATHS := $(library_lib)/SDL2-2.28.0/out
LIBRARIES := -lSDL2 -lm

target_exec := source

build_dir := ./build
src := ./

srcs = $(wildcard $(src)/*.c)
objs = $(addprefix $(build_dir)/, $(notdir $(srcs:.c=.o)))

all: $(target_exec)

release: CFLAGS += -O3
release: $(target_exec)

debug: CFLAGS += -DDEBUG -g
debug: $(target_exec)

$(target_exec): $(objs)
	mkdir -p $(dir $@)
	gcc $(objs) -o $@ $(LDFLAGS) $(LIBRARY_PATHS) $(LIBRARIES)
	
$(build_dir)/%.o: $(src)/%.c
	mkdir -p $(dir $@)
	gcc $(cppflags) $(INCLUDES) $(CFLAGS) -c $< -o $@ $(LIBRARY_PATHS) $(LIBRARIES)
	
.PHONY: clean
clean:
	rm -r $(build_dir)
	
-include $(DEPS)