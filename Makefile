generator: generator.cxx
	g++ -std=c++20 -O3 generator.cxx -o generator $(pkg-config --cflags --libs nlohmann_json)
