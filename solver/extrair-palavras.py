import os

words = []

path = "../pt-br/listas/"


for file in os.scandir(path):
    if file.is_file():
        with open(file.path, "r") as f:
            for line in f:
                if len(line) == 6:
                    words.append(line)

with open("word_list.txt", "w") as f:
    for w in words:
        f.write(w)
