CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"
CONTIKI_PROJECT = climbnet 
all: $(CONTIKI_PROJECT)
CONTIKI_SOURCEFILES += dt_timesync.c 

CFS=fat

MODULES+=core/cfs/fat

CONTIKI_WITH_DTN=1
CONTIKI = ../../..
include $(CONTIKI)/Makefile.include
