# termo-solver

## Interface TUI (`solver/termo_tui.cpp`)

Interface de terminal para o Termo, com as sugestões do solver atualizadas em
tempo real. Ela reutiliza as funções de `solver/termo_game.cpp` sem alterá-las
(`carregar_palavras`, `sortear`, `word_state`, `calc_valor_esperado`,
`filtrar_palavras`).

### Compilar

```sh
cd solver
g++ -O2 -std=c++17 -pthread termo_tui.cpp -o termo_tui
```

## Modos

| Modo | Comando | Funcionamento |
| --- | --- | --- |
| Local | `./termo_tui` | o solver sorteia a palavra secreta e você joga nela |
| Oficial | `./termo_tui --oficial` | você informa seu palpite e as cores vistas em https://term.ooo |
| Oficial + navegador | `./termo-oficial` | abre https://term.ooo e inicia o modo oficial |

### Modo local (palavra sorteada)

```sh
cd solver
./termo_tui
```

O solver sorteia a palavra secreta com `sortear`, permite até 6 tentativas e
revela o segredo no fim. As sugestões da direita são recalculadas após cada
palpite.

### Modo oficial (jogando em https://term.ooo)

```sh
cd solver
./termo_tui --oficial
```

Você faz o palpite no Termo oficial e informa aqui o palpite e as cores que
apareceram. O solver filtra as palavras candidatas e sugere os próximos
palpites.

**Fluxo:**

1. Digite o palpite (5 letras) e tecle `ENTER`.
2. Para cada letra, informe a cor vista no Termo:
   - `V` ou `1` = verde (posição correta)
   - `A` ou `2` = amarelo (existe em outra posição)
   - `B` ou `3`/`0` = branco/cinza (não existe)
3. Tecle `ENTER` para confirmar o resultado.
4. As sugestões e a quantidade de candidatas são recalculadas.
5. Se errar alguma cor, use `BACKSPACE` antes de confirmar ou `Ctrl-Z` para
   desfazer uma jogada já confirmada.

No modo oficial o filtro trata letras repetidas como o Termo real (cada
ocorrência só pode ser usada uma vez). As funções originais continuam intactas.

### Modo oficial + navegador

```sh
cd solver
./termo-oficial
```

O script abre https://term.ooo no navegador e executa o modo oficial. Ele
compila o `termo_tui` automaticamente caso o binário não exista ou esteja
desatualizado.

As mesmas opções também podem ser usadas diretamente:

```sh
./termo_tui --oficial --abrir  # abre a página e inicia o modo oficial
./termo_tui --abrir            # abre a página e inicia o modo local
```

### Tela

- **Centro**: palpite atual e tentativas anteriores. Cada letra é colorida como
  no Termo: verde = posição correta, amarelo = existe em outra posição, branco =
  não existe na palavra.
- **Direita**: as 10 palavras com maior valor calculado por
  `calc_valor_esperado`, recalculadas após cada tentativa (com barra de
  progresso durante o cálculo).
- **Esquerda**: tentativas, palavras candidatas, legenda de cores e atalhos.

### Teclas

| Tecla | Ação |
| --- | --- |
| `A`–`Z` | digitar o palpite (até 5 letras) |
| `V`/`A`/`B` ou `1`/`2`/`3` | informar verde/amarelo/branco no modo oficial |
| `ENTER` | confirmar o palpite ou o resultado |
| `BACKSPACE` | apagar a última letra/cor |
| `Ctrl-Z` | desfazer a última jogada (modo oficial) |
| `Ctrl-N` | novo jogo / nova análise |
| `Ctrl-U` | limpar o palpite ou as cores digitadas |
| `ESC` / `Ctrl-C` | sair |

### Lista de palavras

Qualquer modo aceita um caminho alternativo para a lista:

```sh
./termo_tui --oficial /caminho/para/word_list.txt
./termo-oficial /caminho/para/word_list.txt
```
