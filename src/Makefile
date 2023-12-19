# Final Project
# Elijah Delavar
# CS 360
# 12/10/2023

CLIENT = myftp
SERVER = myftpserve
COBJS = myftp.c myftp.h
SOBJS = myftpserve.c myftp.h
FLAGS = gcc

all: $(CLIENT) $(SERVER)

$(CLIENT): ${COBJS}
	${FLAGS} -o ${CLIENT} ${COBJS}

$(SERVER): ${SOBJS}
	${FLAGS} -o ${SERVER} ${SOBJS}

clean:
	rm $(CLIENT)
	rm $(SERVER)
