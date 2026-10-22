#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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
  return 0;
}
