#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN "\033[36m"
#define WHITE "\033[37m"

// carregar vetor de palavras
std::vector<std::string> carregar_palavras(std::string path) {
  std::string linha;
  std::vector<std::string> lista_palavras;
  std::ifstream arquivo(path);
  if (arquivo.is_open()) {
    while (getline(arquivo, linha)) {
      lista_palavras.emplace_back(linha);
    }
    arquivo.close();
  } else {
    std::cout << "nao foi possivel abrir o arquivo" << std::endl;
  }
  return lista_palavras;
}

// sortear palavra
std::string sortear(std::vector<std::string> lista_palavras) {
  srand(time(0));
  int randomNum = rand() % lista_palavras.size();
  return lista_palavras[randomNum];
}

int main() {
  // carregar lista de palavras
  std::vector<std::string> lista_palavras;
  lista_palavras = carregar_palavras("./word_list.txt");
  std::string palavra;
  palavra = sortear(lista_palavras);
  std::cout << palavra << std::endl;
  bool solved = false;
  std::string input;
  while (solved == false) {
    std::cin >> input;
    int verificacao = 0;
    for (size_t i = 0; i < input.length(); i++) {
      int state = 0;
      for (size_t j = 0; j < palavra.length(); j++) {
        if (input[i] == palavra[j]) {
          if (i == j) {
            state = 2; // green
          }
          if (i != j && state != 2) {
            state = 1;
          }
        }
      }
      switch (state) {

      case 0:
        std::cout << input[i];
        break;
      case 1:
        std::cout << YELLOW << input[i] << RESET;
        break;
      case 2:
        std::cout << GREEN << input[i] << RESET;
        verificacao += 1;
        break;
      }
    }
    std::cout << "" << std::endl;
    if (verificacao == 5) {
      std::cout << "ganhou" << std::endl;
      break;
    }
  }
  return 0;
}
