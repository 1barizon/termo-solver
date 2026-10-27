#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
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

std::vector<std::pair<std::string, int>>
calc_valor_esperado(std::vector<std::string> lista_palavras) {
  int contador_padrao[243] = {0}; // vetor para contar ocorrencia de combinacoes
  int index = 0;
  std::vector<std::pair<std::string, int>> palavra_valor_esperado;
  for (const std::string &guess : lista_palavras) {
    std::cout << "avaliando: " << index << "/" << lista_palavras.size()
              << std::endl;
    memset(contador_padrao, 0,
           243 *
               sizeof(int)); // zerando o vetor de contador para a nova palavra
    for (const std::string &key : lista_palavras) {
      std::array<int, 5> estado;
      estado = word_state(key, guess);
      int indice = converter_base3(estado);
      if (indice >= 0 && indice < 243) {
        contador_padrao[indice]++;
      } else {
        std::cerr << "Aviso: Índice inválido gerado: " << indice << std::endl;
      }
    }
    index++; // calcular o valor esperando
    float valor_esperado = 0.0;
    for (double ct : contador_padrao) {
      if (ct > 0) {
        double p = ct / lista_palavras.size();
        valor_esperado -= p * std::log2(p);
      }
    }

    // ordenar ao inserir
    auto it = std::lower_bound(
        palavra_valor_esperado.begin(), palavra_valor_esperado.end(),
        valor_esperado, [](const std::pair<std::string, int> &a, int b) {
          return a.second > b; // '>' garante a ordem decrescente pelo número
        });

    //  Agora o insert vai funcionar perfeitamente sem erros!
    palavra_valor_esperado.insert(it, {guess, valor_esperado});
    if (palavra_valor_esperado.size() > 10) {
      palavra_valor_esperado.pop_back();
    }
  }
  return palavra_valor_esperado;
}

std::vector<std::string>
filtrar_palavras(const std::vector<std::string> &lista_atual, std::string input,
                 std::array<int, 5> input_state) {
  std::vector<std::string> nova_lista;
  for (const std::string &candidato : lista_atual) {
    std::array<int, 5> estado_simulado = word_state(candidato, input);
    if (estado_simulado == input_state) {
      nova_lista.push_back(candidato);
    }
  }
  return nova_lista;
}

int main() {
  // carregar lista de palavras
  std::vector<std::string> lista_palavras;
  lista_palavras = carregar_palavras("./word_list.txt");
  std::string palavra = sortear(lista_palavras); // sortear palavra
  bool ganho = false;
  std::string palpite;
  while (ganho == false) {
    std::vector<std::pair<std::string, int>> palavra_valor_esperado =
        calc_valor_esperado(lista_palavras);
    std::cout << "Top 10 palavras para comecar" << std::endl;
    for (const auto &[word, value] : palavra_valor_esperado) {
      std::cout << word << " | E: " << value << "\n";
    }
    std::cout << "insira seu palpite" << std::endl;
    std::cin >> palpite;
    std::array<int, 5> state = word_state(palavra, palpite);
    if (palpite == palavra) {
      ganho = true;
      break;
    } else {
      lista_palavras = filtrar_palavras(lista_palavras, palpite, state);
    }
  }

  return 0;
}
