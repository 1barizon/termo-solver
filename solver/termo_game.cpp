#include <array>
#include <cmath>
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

std::array<int, 5> word_state(std::string palavra, std::string input) {

  std::array<int, 5> state = {0, 0, 0, 0, 0};
  for (size_t i = 0; i < 5; i++) {
    for (size_t j = 0; j < 5; j++) {
      if (input[i] == palavra[j]) {
        if (i == j) {
          state[i] = 2; // posicao correta
        }
        if (i != j && state[i] != 2) {
          state[i] = 1;
        }
      }
    }
  }
  return state;
}

// retorna qual o index de uma combinacao em um vetor de 243 possibilidades
int converter_base3(std::array<int, 5> state) {
  int padrao_index = 0;
  int multiplicador = 81;
  for (int i = 0; i < 5; i++) {
    padrao_index += state[i] * multiplicador;
    multiplicador /= 3;
  }
  return padrao_index;
}

int main() {
  // carregar lista de palavras
  std::vector<std::string> lista_palavras;
  lista_palavras = carregar_palavras("./word_list.txt");
  std::string palavra = sortear(lista_palavras);

  int contador_padrao[243] = {0};
  std::string input = "coisa";

  for (std::string s : lista_palavras) {
    std::array<int, 5> estado;
    estado = word_state(s, input);
    int indice = converter_base3(estado);
    if (indice >= 0 && indice < 243) {
      contador_padrao[indice]++;
    } else {
      std::cerr << "Aviso: Índice inválido gerado: " << indice << std::endl;
      for (int state : estado) {
        std::cout << state << std::endl;
      }
    }
  }

  std::array<int, 5> state_guess = word_state(palavra, input);
  int index_guess = converter_base3(state_guess);
  float count_padrao = contador_padrao[index_guess];
  float prob = count_padrao / lista_palavras.size();

  std::cout << "probabilidade desse padrao :" << prob << std::endl;
  std::cout << "informacao desse padrao :" << -(std::log2(prob)) << std::endl;

  return 0;
}
