import os
import unicodedata

words = []

path = "../pt-br/listas/"


def remover_acento(texto):
    nfdk = unicodedata.normalize("NFKD", texto)
    sem_acento = "".join([c for c in nfdk if not unicodedata.combining(c)])
    return sem_acento


for file in os.scandir(path):
    if file.is_file():
        with open(file.path, "r") as f:
            for line in f:
                if len(line) == 6:
                    line = line.lower()
                    line = remover_acento(line)
                    words.append(line)

with open("word_list.txt", "w") as f:
    for w in words:
        f.write(w)
