// termo_tui.cpp - interface TUI para o jogo Termo
//
// Reutiliza SEM MODIFICAR as funcoes de termo_game.cpp:
//   carregar_palavras, sortear, word_state, converter_base3,
//   calc_valor_esperado, filtrar_palavras
//
// Modos:
//   ./termo_tui                 jogo local (sorteia a palavra)
//   ./termo_tui --oficial       voce informa o palpite e as cores do Termo
//                               oficial; o solver filtra as candidatas
//   ./termo_tui --oficial --abrir   idem, abrindo https://term.ooo
//
// Compilar:
//   g++ -O2 -std=c++17 -pthread termo_tui.cpp -o termo_tui
//
// Executar (de dentro de solver/):
//   ./termo_tui [--oficial] [--abrir] [arquivo_de_palavras]

#define main termo_game_original_main
#include "termo_game.cpp"
#undef main

// remove as macros de cor definidas no arquivo original para evitar conflito
#undef RESET
#undef RED
#undef GREEN
#undef YELLOW
#undef BLUE
#undef MAGENTA
#undef CYAN
#undef WHITE

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <thread>

#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Cores / estilos
// ---------------------------------------------------------------------------
namespace ui {
constexpr const char *RESET = "\x1b[0m";
constexpr const char *BORDER = "\x1b[38;5;244m";
constexpr const char *TITLE = "\x1b[1;36m";
constexpr const char *DIM = "\x1b[38;5;244m";
constexpr const char *BOLD = "\x1b[1m";
constexpr const char *GREEN = "\x1b[1;32m";
constexpr const char *RED = "\x1b[1;31m";
constexpr const char *YELLOW = "\x1b[1;33m";
constexpr const char *CYAN = "\x1b[36m";

// cores das pecas: verde = certa, amarelo = existe em outra posicao,
// branco = nao existe na palavra
constexpr const char *TILE_CORRECT = "\x1b[30;48;5;71m";
constexpr const char *TILE_PRESENT = "\x1b[30;48;5;179m";
constexpr const char *TILE_ABSENT = "\x1b[30;48;5;15m";
constexpr const char *TILE_TYPING = "\x1b[97;48;5;238m";
constexpr const char *TILE_CURSOR = "\x1b[97;48;5;240m";
constexpr const char *TILE_EMPTY = "\x1b[38;5;240;48;5;236m";
} // namespace ui

// ---------------------------------------------------------------------------
// Desvio da saida do calc_valor_esperado (que imprime progresso no cout)
// para nao corromper a tela. Cada linha conta como uma palavra avaliada.
// ---------------------------------------------------------------------------
struct SinkBuf : std::streambuf {
  std::atomic<long> lines{0};

  void reset() { lines.store(0, std::memory_order_relaxed); }

protected:
  int overflow(int c) override {
    if (c != EOF && c == '\n')
      lines.fetch_add(1, std::memory_order_relaxed);
    return c;
  }

  std::streamsize xsputn(const char *s, std::streamsize n) override {
    long count = 0;
    for (std::streamsize i = 0; i < n; i++)
      if (s[i] == '\n')
        count++;
    if (count)
      lines.fetch_add(count, std::memory_order_relaxed);
    return n;
  }

  int sync() override { return 0; }
};

static SinkBuf g_sink;
static std::atomic<bool> g_computing{false};
static std::atomic<size_t> g_total{0};

// ---------------------------------------------------------------------------
// Worker que recalcula as sugestoes em segundo plano
// ---------------------------------------------------------------------------
struct WorkerState {
  std::mutex mtx;
  std::condition_variable cv;
  bool stop = false;
  bool has_request = false;
  std::vector<std::string> request;
  int request_gen = -1;
  std::vector<std::pair<std::string, int>> result;
  int result_gen = -1;
};

static WorkerState g_worker;

static void request_suggestions(std::vector<std::string> list, int gen) {
  {
    std::lock_guard<std::mutex> lk(g_worker.mtx);
    g_worker.request = std::move(list);
    g_worker.request_gen = gen;
    g_worker.has_request = true;
  }
  g_worker.cv.notify_one();
}

static void worker_main() {
  for (;;) {
    std::vector<std::string> list;
    int gen = -1;
    {
      std::unique_lock<std::mutex> lk(g_worker.mtx);
      g_worker.cv.wait(
          lk, [] { return g_worker.stop || g_worker.has_request; });
      if (g_worker.stop)
        return;
      list = std::move(g_worker.request);
      gen = g_worker.request_gen;
      g_worker.has_request = false;
    }

    g_sink.reset();
    g_total.store(list.size(), std::memory_order_relaxed);
    g_computing.store(true, std::memory_order_relaxed);

    // funcao original, sem nenhuma alteracao
    std::vector<std::pair<std::string, int>> res =
        calc_valor_esperado(list);

    {
      std::lock_guard<std::mutex> lk(g_worker.mtx);
      if (g_worker.stop)
        return;
      g_worker.result = std::move(res);
      g_worker.result_gen = gen;
    }
    g_computing.store(false, std::memory_order_relaxed);
  }
}

// ---------------------------------------------------------------------------
// Estado do jogo
// ---------------------------------------------------------------------------
enum class Phase { Playing, Won, Lost };

struct Guess {
  std::string word;
  std::array<int, 5> state;
};

struct Snap {
  bool computing = false;
  long done = 0;
  size_t total = 0;
  int result_gen = -1;
  std::vector<std::pair<std::string, int>> result;
};

static std::vector<std::string> g_all_words;
static std::string g_secret;
static std::vector<Guess> g_history;
static std::string g_input;
static std::vector<std::string> g_candidates;
static int g_gen = 0;
static Phase g_phase = Phase::Playing;
static std::string g_hint;
static std::chrono::steady_clock::time_point g_hint_until;

// modo oficial: o usuario informa o palpite e o resultado visto no Termo
static bool g_official = false;
static bool g_feedback_mode = false;
static int g_feedback_pos = 0;
static std::array<int, 5> g_feedback_state = {0, 0, 0, 0, 0};

// ---------------------------------------------------------------------------
// Utilidades de texto / tela
// ---------------------------------------------------------------------------
static std::string to_upper(std::string s) {
  for (char &c : s)
    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  return s;
}

static size_t vis_len(const std::string &s) {
  size_t width = 0;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == 0x1b) { // sequencia ANSI: nao ocupa coluna
      size_t j = i + 1;
      if (j < s.size() && s[j] == '[') {
        j++;
        while (j < s.size() && !(s[j] >= 0x40 && s[j] <= 0x7e))
          j++;
        if (j < s.size())
          j++;
      }
      i = j;
      continue;
    }
    if ((c & 0xC0) != 0x80) // conta apenas o primeiro byte UTF-8
      width++;
    i++;
  }
  return width;
}

static std::string clip_visible(const std::string &s, size_t maxw) {
  std::string out;
  size_t width = 0;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == 0x1b) {
      size_t j = i + 1;
      if (j < s.size() && s[j] == '[') {
        j++;
        while (j < s.size() && !(s[j] >= 0x40 && s[j] <= 0x7e))
          j++;
        if (j < s.size())
          j++;
      }
      out.append(s, i, j - i);
      i = j;
      continue;
    }
    size_t len = 1;
    if ((c & 0xE0) == 0xC0)
      len = 2;
    else if ((c & 0xF0) == 0xE0)
      len = 3;
    else if ((c & 0xF8) == 0xF0)
      len = 4;
    if (width + 1 > maxw)
      break;
    out.append(s, i, len);
    i += len;
    width++;
  }
  return out;
}

static std::string pad_visible(const std::string &s, size_t w) {
  size_t len = vis_len(s);
  if (len >= w)
    return clip_visible(s, w);
  return s + std::string(w - len, ' ');
}

static std::string center_visible(const std::string &s, size_t w) {
  size_t len = vis_len(s);
  if (len >= w)
    return clip_visible(s, w);
  size_t left = (w - len) / 2;
  return std::string(left, ' ') + s + std::string(w - len - left, ' ');
}

struct TermSize {
  int w = 80;
  int h = 24;
};

static TermSize get_term_size() {
  TermSize ts;
  winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
      ws.ws_row > 0) {
    ts.w = ws.ws_col;
    ts.h = ws.ws_row;
  }
  return ts;
}

// ---------------------------------------------------------------------------
// Paineis com borda
// ---------------------------------------------------------------------------
struct Panel {
  int w = 0;
  std::string title;
  std::vector<std::string> body;
};

static std::vector<std::string> render_panel(const Panel &p, int h) {
  std::vector<std::string> rows;
  if (h < 3 || p.w < 4)
    return rows;
  int cw = p.w - 2;

  auto hline = [](int n) {
    std::string s;
    for (int i = 0; i < n; i++)
      s += "\u2500";
    return s;
  };

  std::string t = " " + p.title + " ";
  size_t tw = vis_len(t);
  std::string top = std::string(ui::BORDER) + "\u250c" + ui::TITLE + t +
                    ui::BORDER;
  if (tw < static_cast<size_t>(cw))
    top += hline(cw - static_cast<int>(tw));
  top += "\u2510" + std::string(ui::RESET);
  rows.push_back(top);

  for (int i = 0; i < h - 2; i++) {
    std::string line = i < static_cast<int>(p.body.size()) ? p.body[i] : "";
    line = pad_visible(line, cw);
    rows.push_back(std::string(ui::BORDER) + "\u2502" + ui::RESET + line +
                   ui::BORDER + "\u2502" + ui::RESET);
  }

  std::string bottom = std::string(ui::BORDER) + "\u2514" + hline(cw) +
                       "\u2518" + ui::RESET;
  rows.push_back(bottom);
  return rows;
}

// ---------------------------------------------------------------------------
// Desenho das pecas
// ---------------------------------------------------------------------------
static std::string make_tile(const char *color, char letter) {
  std::string t(color);
  t += ' ';
  t += letter;
  t += ' ';
  t += ui::RESET;
  return t;
}

// Feedback com a regra do Termo oficial: cada ocorrencia da letra so pode ser
// usada uma vez (verde primeiro, depois amarelo). Usado apenas no modo
// oficial; as funcoes originais continuam intactas.
static std::array<int, 5> official_feedback(const std::string &secret,
                                            const std::string &guess) {
  std::array<int, 5> state = {0, 0, 0, 0, 0};
  std::array<bool, 5> used = {false, false, false, false, false};

  for (int i = 0; i < 5; i++) {
    if (guess[i] == secret[i]) {
      state[i] = 2;
      used[i] = true;
    }
  }

  for (int i = 0; i < 5; i++) {
    if (state[i] == 2)
      continue;
    for (int j = 0; j < 5; j++) {
      if (!used[j] && guess[i] == secret[j]) {
        state[i] = 1;
        used[j] = true;
        break;
      }
    }
  }
  return state;
}

static std::vector<std::string>
filter_candidates_official(const std::vector<std::string> &list,
                           const std::string &guess,
                           const std::array<int, 5> &state) {
  std::vector<std::string> out;
  for (const std::string &candidate : list) {
    if (candidate.size() != 5 || guess.size() != 5)
      continue;
    if (official_feedback(candidate, guess) == state)
      out.push_back(candidate);
  }
  return out;
}

static std::string board_row(int r, int cw) {
  std::string row;
  bool is_input_row =
      (r == static_cast<int>(g_history.size()) && g_phase == Phase::Playing);
  row += is_input_row ? "> " : "  ";

  for (int i = 0; i < 5; i++) {
    if (i > 0)
      row += ' ';

    if (r < static_cast<int>(g_history.size())) {
      int st = g_history[r].state[i];
      char letter = static_cast<char>(
          toupper(static_cast<unsigned char>(g_history[r].word[i])));
      const char *color = st == 2   ? ui::TILE_CORRECT
                          : st == 1 ? ui::TILE_PRESENT
                                    : ui::TILE_ABSENT;
      row += make_tile(color, letter);
    } else if (is_input_row && g_official && g_feedback_mode &&
               g_input.size() == 5) {
      char letter = static_cast<char>(
          toupper(static_cast<unsigned char>(g_input[i])));
      const char *color;
      if (i < g_feedback_pos) {
        int st = g_feedback_state[i];
        color = st == 2   ? ui::TILE_CORRECT
                : st == 1 ? ui::TILE_PRESENT
                          : ui::TILE_ABSENT;
      } else if (i == g_feedback_pos) {
        color = ui::TILE_CURSOR;
      } else {
        color = ui::TILE_TYPING;
      }
      row += make_tile(color, letter);
    } else if (is_input_row) {
      if (i < static_cast<int>(g_input.size())) {
        char letter = static_cast<char>(
            toupper(static_cast<unsigned char>(g_input[i])));
        row += make_tile(ui::TILE_TYPING, letter);
      } else if (i == static_cast<int>(g_input.size())) {
        row += make_tile(ui::TILE_CURSOR, '_');
      } else {
        row += make_tile(ui::TILE_EMPTY, ' ');
      }
    } else {
      row += make_tile(ui::TILE_EMPTY, ' ');
    }
  }
  return center_visible(row, cw);
}

static std::string info_line(const std::string &label,
                             const std::string &value, int cw) {
  std::string left = " " + label;
  size_t l = vis_len(left);
  size_t v = vis_len(value);
  if (l + v + 1 <= static_cast<size_t>(cw))
    return left + std::string(cw - l - v, ' ') + value;
  return clip_visible(left + " " + value, cw);
}

static int progress_pct(const Snap &snap) {
  if (snap.total == 0)
    return 0;
  long done = snap.done;
  if (done < 0)
    done = 0;
  if (done > static_cast<long>(snap.total))
    done = static_cast<long>(snap.total);
  return static_cast<int>(done * 100 / static_cast<long>(snap.total));
}

// ---------------------------------------------------------------------------
// Frame completo
// ---------------------------------------------------------------------------
static std::string render_frame(const Snap &snap) {
  TermSize ts = get_term_size();
  std::string out;

  if (ts.w < 52 || ts.h < 14) {
    out += "\x1b[H\x1b[2J";
    out += "Terminal pequeno: " + std::to_string(ts.w) + "x" +
           std::to_string(ts.h) + " (minimo 52x14)\r\n";
    return out;
  }

  const int W = ts.w;
  const int H = ts.h;
  const int panel_h = H - 2;
  const int content_h = panel_h - 2;

  int info_w = 22;
  int right_w = std::clamp(W / 4, 28, 40);
  int center_w = W - info_w - right_w;
  if (center_w < 25) {
    info_w = 0;
    center_w = W - right_w;
  }
  const int center_cw = center_w - 2;
  const int right_cw = right_w - 2;
  const int info_cw = info_w > 0 ? info_w - 2 : 0;

  // ---- painel central: tabuleiro e palpite -------------------------------
  std::vector<std::string> center_body;
  const int core_h = 10; // 1 espaco + 6 linhas + 1 espaco + 2 mensagens
  int top_pad = (content_h - core_h) / 2;
  if (top_pad < 0)
    top_pad = 0;
  for (int i = 0; i < top_pad; i++)
    center_body.push_back("");

  center_body.push_back("");
  for (int r = 0; r < 6; r++)
    center_body.push_back(board_row(r, center_cw));
  center_body.push_back("");

  std::string msg1;
  std::string msg2;
  if (!g_hint.empty()) {
    msg1 = std::string(ui::YELLOW) + g_hint + ui::RESET;
  } else if (g_official) {
    if (g_phase == Phase::Won) {
      msg1 = std::string(ui::GREEN) + "ACERTOU!" + ui::RESET;
      msg2 = std::string(ui::DIM) + "palavra: " +
             to_upper(g_history.back().word) + ui::RESET;
    } else if (g_phase == Phase::Lost) {
      msg1 = std::string(ui::RED) + "FIM DE JOGO" + ui::RESET;
      msg2 = std::string(ui::DIM) + "6 tentativas usadas" + ui::RESET;
    } else if (g_feedback_mode) {
      if (g_feedback_pos == 5) {
        msg1 = std::string(ui::CYAN) + "ENTER para confirmar" + ui::RESET;
        msg2 = std::string(ui::DIM) + "BACKSPACE corrige" + ui::RESET;
      } else {
        msg1 = std::string(ui::CYAN) + "V verde  A amarelo" + ui::RESET;
        msg2 = std::string(ui::DIM) + "B branco  (ou 1/2/3)" + ui::RESET;
      }
    } else {
      msg1 = std::string(ui::DIM) + "digite 5 letras" + ui::RESET;
      msg2 = std::string(ui::DIM) + "ENTER confirma" + ui::RESET;
    }
  } else if (g_phase == Phase::Won) {
    msg1 = std::string(ui::GREEN) + "ACERTOU!" + ui::RESET;
    msg2 = std::string(ui::DIM) + "a palavra era " + to_upper(g_secret) +
           ui::RESET;
  } else if (g_phase == Phase::Lost) {
    msg1 = std::string(ui::RED) + "FIM DE JOGO" + ui::RESET;
    msg2 = std::string(ui::DIM) + "a palavra era " + to_upper(g_secret) +
           ui::RESET;
  } else if (g_input.size() == 5) {
    msg1 = std::string(ui::CYAN) + "ENTER para confirmar" + ui::RESET;
  } else {
    msg1 = std::string(ui::DIM) + "digite 5 letras" + ui::RESET;
  }
  center_body.push_back(center_visible(msg1, center_cw));
  center_body.push_back(center_visible(msg2, center_cw));

  Panel center;
  center.w = center_w;
  center.title = "PALPITES";
  center.body = std::move(center_body);

  // ---- painel direito: sugestoes ----------------------------------------
  std::vector<std::string> right_body;
  bool fresh = (snap.result_gen == g_gen);
  if (fresh) {
    right_body.push_back(std::string(ui::DIM) + " top 10 por entropia" +
                         ui::RESET);
    right_body.push_back("");
    int max_items = content_h - 2;
    if (max_items < 0)
      max_items = 0;
    if (snap.result.empty()) {
      right_body.push_back(std::string(ui::DIM) + " nenhuma candidata" +
                           ui::RESET);
    } else {
      std::vector<std::string> shown;
      for (int i = 0; i < static_cast<int>(snap.result.size()) &&
                      static_cast<int>(shown.size()) < max_items;
           i++) {
        // a lista de palavras possui duplicatas; evita repetir na tela
        if (std::find(shown.begin(), shown.end(), snap.result[i].first) !=
            shown.end())
          continue;
        shown.push_back(snap.result[i].first);
        char buf[64];
        snprintf(buf, sizeof(buf), " %2d. %-5s   E:%4d",
                 static_cast<int>(shown.size()),
                 to_upper(snap.result[i].first).c_str(), snap.result[i].second);
        right_body.push_back(buf);
      }
    }
  } else {
    int pct = progress_pct(snap);
    right_body.push_back(" calculando sugestões...");
    right_body.push_back("");

    int barw = std::max(8, right_cw - 4);
    int filled = barw * pct / 100;
    std::string bar = " [" + std::string(ui::CYAN) +
                      std::string(filled, '#') + ui::DIM +
                      std::string(barw - filled, '-') + ui::RESET + "]";
    right_body.push_back(bar);
    char buf[80];
    snprintf(buf, sizeof(buf), " %3d%%  %ld/%zu", pct, snap.done,
             snap.total);
    right_body.push_back(buf);
    right_body.push_back("");
    right_body.push_back(std::string(ui::DIM) + " a primeira análise da" +
                         ui::RESET);
    right_body.push_back(std::string(ui::DIM) + " lista completa leva" +
                         ui::RESET);
    right_body.push_back(std::string(ui::DIM) + " alguns segundos." +
                         ui::RESET);
  }

  Panel right;
  right.w = right_w;
  right.title = "SUGESTÕES";
  right.body = std::move(right_body);

  // ---- painel esquerdo: status ------------------------------------------
  Panel info;
  info.w = info_w;
  if (info_w > 0) {
    info.title = "STATUS";

    std::string calc_state;
    if (fresh)
      calc_state = "pronto";
    else if (snap.computing)
      calc_state = std::to_string(progress_pct(snap)) + "%";
    else
      calc_state = "aguardando";

    std::vector<std::string> ib;
    ib.push_back("");
    ib.push_back(info_line("jogadas", std::to_string(g_history.size()) + "/6",
                           info_cw));
    ib.push_back(
        info_line("candidatas", std::to_string(g_candidates.size()), info_cw));
    if (g_official) {
      ib.push_back(info_line("modo", "oficial", info_cw));
    } else {
      ib.push_back(
          info_line("segredo", g_phase == Phase::Playing ? "*****"
                                                         : to_upper(g_secret),
                    info_cw));
    }
    ib.push_back(info_line("cálculo", calc_state, info_cw));
    ib.push_back("");
    if (g_official) {
      ib.push_back(" ENTER  confirmar");
      ib.push_back(" BKSP   apagar");
      ib.push_back(" ^Z     desfazer");
      ib.push_back(" ^N     novo jogo");
      ib.push_back(" ESC    sair");
    } else {
      ib.push_back(" ENTER  enviar");
      ib.push_back(" BKSP   apagar");
      ib.push_back(" ^N     novo jogo");
      ib.push_back(" ESC    sair");
    }
    ib.push_back("");
    ib.push_back(std::string(ui::TILE_CORRECT) + " VERDE " + ui::RESET +
                 " certa");
    ib.push_back(std::string(ui::TILE_PRESENT) + " AMARELA " + ui::RESET +
                 " outra");
    ib.push_back(std::string(ui::TILE_ABSENT) + " BRANCA " + ui::RESET +
                 " ausente");
    info.body = std::move(ib);
  }

  std::vector<std::string> info_rows = render_panel(info, panel_h);
  std::vector<std::string> center_rows = render_panel(center, panel_h);
  std::vector<std::string> right_rows = render_panel(right, panel_h);

  // ---- cabecalho --------------------------------------------------------
  int pct = progress_pct(snap);
  std::string status;
  if (g_phase == Phase::Won)
    status = "acertou";
  else if (g_phase == Phase::Lost)
    status = "fim de jogo";
  else if (fresh)
    status = "sugestões prontas";
  else if (snap.computing)
    status = "calculando " + std::to_string(pct) + "%";
  else
    status = "aguardando";

  int shown_attempt =
      g_phase == Phase::Playing
          ? std::min<int>(static_cast<int>(g_history.size()) + 1, 6)
          : static_cast<int>(g_history.size());
  std::string left = g_official ? " TERMO OFICIAL" : " TERMO SOLVER";
  std::string right_txt = "tentativa " + std::to_string(shown_attempt) + "/6" +
                          "   restam " + std::to_string(g_candidates.size()) +
                          "   " + status;
  std::string header;
  if (vis_len(left) + vis_len(right_txt) + 1 <= static_cast<size_t>(W)) {
    header = left + std::string(W - vis_len(left) - vis_len(right_txt), ' ') +
             right_txt;
  } else {
    header = clip_visible(left + "  " + right_txt, W);
  }
  header = pad_visible(header, W);
  out += "\x1b[1;1H\x1b[1;97;44m" + header + "\x1b[0m";

  // ---- corpo ------------------------------------------------------------
  for (int r = 0; r < panel_h; r++) {
    out += "\x1b[" + std::to_string(r + 2) + ";1H";
    if (info_w > 0)
      out += info_rows[r];
    out += center_rows[r];
    out += right_rows[r];
  }

  // ---- rodape -----------------------------------------------------------
  std::string footer =
      g_official ? " ENTER confirmar   V/A/B cores   Ctrl-Z desfazer   "
                   "Ctrl-N novo   ESC sair"
                 : " ENTER enviar   BKSP apagar   Ctrl-N novo jogo   ESC sair";
  footer = pad_visible(footer, W);
  out += "\x1b[" + std::to_string(H) + ";1H\x1b[38;5;250;48;5;236m" + footer +
         "\x1b[0m\x1b[J";

  return out;
}

// ---------------------------------------------------------------------------
// Terminal
// ---------------------------------------------------------------------------
static termios g_orig_termios;
static bool g_raw_active = false;

static void restore_terminal() {
  if (g_raw_active) {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
    g_raw_active = false;
  }
  const char msg[] = "\x1b[0m\x1b[?25h\n";
  (void)!write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}

static void on_signal(int) {
  restore_terminal();
  _exit(0);
}

static void set_hint(const std::string &hint, int ms = 1500) {
  g_hint = hint;
  g_hint_until =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
}

static Snap take_snapshot() {
  Snap snap;
  snap.computing = g_computing.load(std::memory_order_relaxed);
  snap.done = g_sink.lines.load(std::memory_order_relaxed);
  snap.total = g_total.load(std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lk(g_worker.mtx);
    snap.result_gen = g_worker.result_gen;
    snap.result = g_worker.result;
  }
  return snap;
}

static void new_game() {
  if (!g_official)
    g_secret = sortear(g_all_words);
  g_history.clear();
  g_input.clear();
  g_feedback_mode = false;
  g_feedback_pos = 0;
  g_feedback_state = {0, 0, 0, 0, 0};
  g_candidates = g_all_words;
  g_phase = Phase::Playing;
  g_hint.clear();
  g_gen++;
  request_suggestions(g_candidates, g_gen);
}

static void submit_guess() {
  if (g_phase != Phase::Playing)
    return;
  if (g_input.size() != 5) {
    set_hint("digite 5 letras");
    return;
  }

  std::array<int, 5> state = word_state(g_secret, g_input);
  g_history.push_back({g_input, state});
  bool won = (g_input == g_secret);
  g_input.clear();

  if (won) {
    g_phase = Phase::Won;
    return;
  }

  g_candidates = filtrar_palavras(g_candidates, g_history.back().word, state);
  if (g_history.size() >= 6) {
    g_phase = Phase::Lost;
    return;
  }

  g_gen++;
  request_suggestions(g_candidates, g_gen);
}

// ---- modo oficial: palpite + cores informadas pelo usuario -----------------
static void begin_feedback() {
  if (g_phase != Phase::Playing)
    return;
  if (g_input.size() != 5) {
    set_hint("digite 5 letras");
    return;
  }
  g_feedback_mode = true;
  g_feedback_pos = 0;
  g_feedback_state = {0, 0, 0, 0, 0};
  g_hint.clear();
}

static void submit_feedback() {
  if (g_phase != Phase::Playing || !g_feedback_mode)
    return;
  if (g_feedback_pos != 5) {
    set_hint("informe as 5 cores");
    return;
  }

  std::string guess = g_input;
  std::array<int, 5> state = g_feedback_state;
  bool all_green = state[0] == 2 && state[1] == 2 && state[2] == 2 &&
                   state[3] == 2 && state[4] == 2;

  g_history.push_back({guess, state});
  g_input.clear();
  g_feedback_mode = false;
  g_feedback_pos = 0;
  g_feedback_state = {0, 0, 0, 0, 0};

  if (all_green) {
    g_phase = Phase::Won;
    return;
  }

  g_candidates = filter_candidates_official(g_candidates, guess, state);
  if (g_history.size() >= 6) {
    g_phase = Phase::Lost;
    return;
  }

  if (g_candidates.empty())
    set_hint("nenhuma candidata: confira as cores", 2500);

  g_gen++;
  request_suggestions(g_candidates, g_gen);
}

static void undo_last() {
  if (g_history.empty()) {
    set_hint("nada para desfazer");
    return;
  }

  g_history.pop_back();
  g_phase = Phase::Playing;
  g_input.clear();
  g_feedback_mode = false;
  g_feedback_pos = 0;
  g_feedback_state = {0, 0, 0, 0, 0};

  g_candidates = g_all_words;
  for (const Guess &h : g_history)
    g_candidates = filter_candidates_official(g_candidates, h.word, h.state);

  g_gen++;
  request_suggestions(g_candidates, g_gen);
  set_hint("jogada desfeita");
}

struct Options {
  bool official = false;
  bool open_page = false;
  bool help = false;
  std::string path;
};

static std::string default_word_list() {
  const char *candidates[] = {"./word_list.txt", "word_list.txt",
                              "solver/word_list.txt",
                              "../solver/word_list.txt"};
  for (const char *p : candidates)
    if (access(p, R_OK) == 0)
      return p;
  return "./word_list.txt";
}

static Options parse_args(int argc, char **argv) {
  Options opt;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--oficial" || arg == "--official" || arg == "-o") {
      opt.official = true;
    } else if (arg == "--abrir" || arg == "--open" || arg == "-b") {
      opt.open_page = true;
    } else if (arg == "--help" || arg == "-h") {
      opt.help = true;
    } else {
      opt.path = arg;
    }
  }
  if (opt.path.empty())
    opt.path = default_word_list();
  return opt;
}

static void open_url(const char *url) {
  std::string cmd = "xdg-open '" + std::string(url) + "' >/dev/null 2>&1 &";
  int rc = std::system(cmd.c_str());
  (void)rc;
}

int main(int argc, char **argv) {
  Options opt = parse_args(argc, argv);
  if (opt.help) {
    printf("uso: termo_tui [--oficial] [--abrir] [arquivo_de_palavras]\n"
           "\n"
           "  --oficial, -o  informa o palpite e as cores vistas no Termo "
           "oficial\n"
           "  --abrir,   -b  abre https://term.ooo no navegador\n"
           "\n"
           "sem --oficial: jogo local com palavra sorteada\n");
    return 0;
  }

  g_official = opt.official;
  std::string path = opt.path;
  g_all_words = carregar_palavras(path);
  if (g_all_words.empty()) {
    fprintf(stderr, "nao foi possivel carregar a lista de palavras: %s\n",
            path.c_str());
    return 1;
  }

  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
    fprintf(stderr, "termo_tui precisa ser executado em um terminal "
                    "interativo.\n");
    return 1;
  }

  if (opt.open_page)
    open_url("https://term.ooo");

  if (tcgetattr(STDIN_FILENO, &g_orig_termios) != 0) {
    fprintf(stderr, "nao foi possivel ler as configuracoes do terminal.\n");
    return 1;
  }

  termios raw = g_orig_termios;
  cfmakeraw(&raw);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1; // 100 ms, usado apenas para redesenhar o progresso
  if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
    fprintf(stderr, "nao foi possivel configurar o terminal.\n");
    return 1;
  }
  g_raw_active = true;

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  signal(SIGHUP, on_signal);

  // silencia todo o cout/cerr das funcoes originais durante o desenho
  std::cout.rdbuf(&g_sink);
  std::cerr.rdbuf(&g_sink);

  std::thread worker(worker_main);
  new_game();

  (void)!write(STDOUT_FILENO, "\x1b[?25l\x1b[2J", 10);

  bool running = true;
  while (running) {
    if (!g_hint.empty() &&
        std::chrono::steady_clock::now() > g_hint_until)
      g_hint.clear();

    Snap snap = take_snapshot();
    std::string frame = render_frame(snap);
    (void)!write(STDOUT_FILENO, frame.data(), frame.size());

    unsigned char buf[64];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0)
      continue;

    for (ssize_t i = 0; i < n && running; i++) {
      unsigned char c = buf[i];

      if (c == 0x1b) { // ESC ou sequencia de tecla especial
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 30000;
        if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
          unsigned char extra[8];
          ssize_t m = read(STDIN_FILENO, extra, sizeof(extra));
          if (m > 0 && (extra[0] == '[' || extra[0] == 'O'))
            continue; // seta/home/end: ignora
        }
        running = false;
        break;
      }

      if (c == 3 || c == 4) { // Ctrl-C / Ctrl-D
        running = false;
        break;
      }
      if (c == 14) { // Ctrl-N: novo jogo
        new_game();
        continue;
      }
      if (c == 26) { // Ctrl-Z: desfazer (modo oficial)
        if (g_official)
          undo_last();
        continue;
      }
      if (c == 21) { // Ctrl-U: limpa o palpite / as cores digitadas
        if (g_official && g_feedback_mode) {
          g_feedback_pos = 0;
          g_feedback_state = {0, 0, 0, 0, 0};
        } else {
          g_input.clear();
        }
        continue;
      }
      if (c == 127 || c == 8) {
        if (g_official && g_feedback_mode) {
          if (g_feedback_pos > 0) {
            g_feedback_pos--;
            g_feedback_state[g_feedback_pos] = 0;
          } else {
            g_feedback_mode = false; // volta a editar o palpite
          }
        } else if (!g_input.empty()) {
          g_input.pop_back();
        }
        continue;
      }
      if (c == 13 || c == 10) {
        if (g_official) {
          if (g_feedback_mode)
            submit_feedback();
          else
            begin_feedback();
        } else {
          submit_guess();
        }
        continue;
      }

      // normaliza letras maiusculas
      unsigned char lc = c;
      if (c >= 'A' && c <= 'Z')
        lc = static_cast<unsigned char>(c - 'A' + 'a');

      if (g_official && g_feedback_mode) {
        // 1/2/3/0 tambem funcionam como atalhos
        if (c == '1')
          lc = 'v';
        else if (c == '2')
          lc = 'a';
        else if (c == '3' || c == '0')
          lc = 'b';

        if (lc == 'v' || lc == 'a' || lc == 'b') {
          if (g_feedback_pos < 5) {
            g_feedback_state[g_feedback_pos] =
                lc == 'v' ? 2 : (lc == 'a' ? 1 : 0);
            g_feedback_pos++;
          }
        }
        continue; // no modo de cores, letras nao editam o palpite
      }

      if (lc >= 'a' && lc <= 'z') {
        if (g_phase == Phase::Playing && g_input.size() < 5)
          g_input.push_back(static_cast<char>(lc));
        continue;
      }
    }
  }

  restore_terminal();
  _exit(0);
}
