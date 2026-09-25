# ZAPM with the curses shim + X11 frontend (RVIP). Run from the repo root:
#   make -f port/Makefile        -> ./zapm-x11
SRC := $(filter-out Visibility.c win.c,$(wildcard *.cpp))
OBJ := $(SRC:%.cpp=port/obj/%.o) port/obj/wcurses.o port/obj/be_x11.o
override CXXFLAGS += -Iport -DZAPM_SHIM -I/opt/X11/include -I/opt/X11/include/freetype2 -std=c++98 -O1 -g -w
override CFLAGS += -Iport -I/opt/X11/include -I/opt/X11/include/freetype2 -O1 -g
zapm-x11: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) -L/opt/X11/lib -lX11 -lXft -lfontconfig
port/obj/%.o: %.cpp *.h port/curses.h
	@mkdir -p port/obj
	$(CXX) $(CXXFLAGS) -c $< -o $@
port/obj/%.o: port/%.c port/curses.h
	@mkdir -p port/obj
	$(CC) $(CFLAGS) -c $< -o $@
clean:
	rm -rf port/obj zapm-x11
