FLAGS:=-std=c++17 -pthread -ltbb -Werror -pedantic-errors -Wall -Wextra -Wpedantic -Wcast-align -Wcast-qual -Wconversion -Wctor-dtor-privacy -Wduplicated-branches -Wduplicated-cond -Wextra-semi -Wfloat-equal -Wlogical-op -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wredundant-decls -Wsign-conversion -Wsign-promo
OUT_NAME:=fdiff
FILES:=source.cpp

bulid:
	g++ -O3 $(FLAGS) $(FILES) -o $(OUT_NAME)
debug:
	g++ -g -no-pie $(FLAGS) $(FILES) -o $(OUT_NAME)
