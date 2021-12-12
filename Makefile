# Address-sanitizer library
ASAN_FLAGS := -lasan
ifneq ($(OS),Windows_NT)
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Darwin)
		# macOS requires different a flag
		ASAN_FLAGS := -fsanitize=address
	endif
endif

gpstelemetry : gpstelemetry.o GPMF_parser.o GPMF_utils.o GPMF_mp4reader.o
		gcc -o $@ gpstelemetry.o GPMF_parser.o GPMF_utils.o GPMF_mp4reader.o $(ASAN_FLAGS)

gpstelemetry.o : gpstelemetry.c
		gcc -g -c gpstelemetry.c
GPMF_mp4reader.o : ./gpmf-parser/demo/GPMF_mp4reader.c ./gpmf-parser/GPMF_parser.h
		gcc -g -c ./gpmf-parser/demo/GPMF_mp4reader.c
GPMF_parser.o : ./gpmf-parser/GPMF_parser.c ./gpmf-parser/GPMF_parser.h
		gcc -g -c ./gpmf-parser/GPMF_parser.c
GPMF_utils.o : ./gpmf-parser/GPMF_utils.c ./gpmf-parser/GPMF_utils.h
		gcc -g -c ./gpmf-parser/GPMF_utils.c
clean :
		rm -f gpstelemetry *.o
