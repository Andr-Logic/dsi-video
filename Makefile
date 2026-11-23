BLOCKSDS	?= /opt/blocksds/core
BLOCKSDSEXT	?= /opt/blocksds/external

NAME		:= dsi_video_player
GAME_TITLE	:= DSi Video Player
GAME_SUBTITLE	:= Passo 1 - info video

LIBS		:= -lnds9
LIBDIRS		:= $(BLOCKSDS)/libs/libnds

include/opt/blocksds/core/sys/default_makefiles/rom_arm9only/Makefile
