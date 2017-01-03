warnings = -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-unused-variable \
		   -Wduplicated-cond -Wdouble-promotion -Wnull-dereference \
		   -Wsuggest-attribute=const
flags = -O3 -std=c++0x
libraries = -lSDL2 -lGLEW -lGL

default:
	g++ main.cc screen.cc imgui/imgui.cpp imgui/imgui_draw.cpp \
		imgui/imgui_demo.cpp -o poly2tri \
		$(flags) $(libraries) $(warnings)
	./poly2tri

lines:
	@wc -l *.*

