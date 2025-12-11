# Kompilator i opcje
CXX = g++
CXXFLAGS = -Wall -std=c++17 -g

# Pliki zrodlowe
SRC = main.cpp chef.cpp client.cpp error_handler.cpp manager.cpp service.cpp single_client.cpp

# Pliki obiektowe
OBJ = $(SRC:.cpp=.o)

# Wynikowy program
TARGET = restauracja

# Regula domyslna
all: $(TARGET)

# Linkowanie programu
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Kompilacja plikwo cpp do obiektow
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Czyszczenie
clean:
	rm -f $(OBJ) $(TARGET)
