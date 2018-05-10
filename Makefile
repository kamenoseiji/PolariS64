#----------------- LIBRARY -------------------
PGDIR = /usr/lib
CPGDIR = $(PGDIR)
PGPLOT= $(PGDIR)/libpgplot.a
CPGPLOT= $(CPGDIR)/libcpgplot.a
BINDIR = /usr/local/bin
#----------------- LINK OPTIONS -------------------
CCOMPL=gcc 
NVCC=nvcc
FCOMPL=gfortran 
#------- Followings are PASS or DIRECTORY -------
PROGS=	polaris_start shm_param shm_alloc shm_init shm_param_view VDIF_store cuda_fft_xspec shm_spec_view shm_power_view bitDist VDIF_sim
GRLIBS= -L/usr/include/X11 -lX11
MATH=	-lm
FFTLIB= -lcufft
CUDALIB= -lcutil
PNGLIB = -lpng
#----------------- MAPPING ------------------------
OBJ_start= polaris_start.o shm_access.o pow2round.o
OBJ_shm_param= shm_param.o shm_init_create.o shm_access.o erase_shm.o
OBJ_shm_alloc= shm_alloc.o shm_init_create.o shm_access.o
OBJ_shm_init = shm_init.o shm_access.o
OBJ_shm_view = shm_param_view.o shm_access.o timesystem.o vdif_head_extract.o
OBJ_spec_view = shm_spec_view.o shm_access.o cpg_setup.o cpg_spec.o
OBJ_power_view = shm_power_view.o shm_access.o cpg_setup.o cpg_power.o
OBJ_VDIF_store = VDIF_store.o shm_access.o VDIFutc.o
OBJ_VDIF_sim = VDIF_sim.o shm_access.o
OBJ_cuda_fft = cuda_fft_xspec.o
OBJ_bitDist = bitDist.o
OBJ_PolariSplit = PolariSplit.o
OBJ_PolariBunch = PolariBunch.o
#----------------- Compile and link ------------------------
polaris_start : $(OBJ_start)
	$(CCOMPL) -o $@ $(OBJ_start)

VDIF_store : $(OBJ_VDIF_store)
	$(CCOMPL) -o $@ $(OBJ_VDIF_store)

VDIF_sim : $(OBJ_VDIF_sim)
	$(CCOMPL) -o $@ $(OBJ_VDIF_sim)

shm_param : $(OBJ_shm_param)
	$(CCOMPL) -o $@ $(OBJ_shm_param)

shm_alloc : $(OBJ_shm_alloc)
	$(CCOMPL) -o $@ $(OBJ_shm_alloc)

shm_init : $(OBJ_shm_init)
	$(CCOMPL) -o $@ $(OBJ_shm_init)

shm_param_view : $(OBJ_shm_view)
	$(CCOMPL) -o $@ $(OBJ_shm_view)

bitDist : $(OBJ_bitDist)
	$(CCOMPL) -o $@ $(OBJ_bitDist) $(MATH)

shm_spec_view : $(OBJ_spec_view)
	$(FCOMPL) -o $@ $(OBJ_spec_view) $(CPGPLOT) $(PGPLOT) $(GRLIBS) $(PNGLIB)

shm_power_view : $(OBJ_power_view)
	$(FCOMPL) -o $@ $(OBJ_power_view) $(CPGPLOT) $(PGPLOT) $(GRLIBS) $(PNGLIB)

cuda_fft_xspec : $(OBJ_cuda_fft)
	$(NVCC) -o $@ $(OBJ_cuda_fft) $(FFTLIB)

clean :
	\rm $(PROGS) *.o a.out core *.trace

all :	$(PROGS)

install:
	@mv $(PROGS) $(BINDIR)

#----------------- Objects ------------------------
#.cu.o:
#	$(NVCC) -c $*.cu
cuda_fft_xspec.o:	cuda_fft_xspec.cu	shm_VDIF.inc cuda_polaris.inc
	$(NVCC) -c cuda_fft_xspec.cu
#bitPower.o:			bitPower.cu
#	$(NVCC) -c bitPower.cu

.c.o:
	$(CCOMPL) -c $*.c

bitDist.o:			bitDist.c			shm_VDIF.inc
VDIF_store.o:		VDIF_store.c		shm_VDIF.inc
VDIF_sim.o:			VDIF_sim.c          shm_VDIF.inc
VDIFutc.o:	        VDIFutc.c	        shm_VDIF.inc
polaris_start.o:	polaris_start.c		shm_VDIF.inc
pow2round.o:		pow2round.c
PolariSplit.o:		PolariSplit.c		shm_VDIF.inc
PolariBunch.o:		PolariBunch.c		shm_VDIF.inc
shm_param.o:		shm_param.c			shm_VDIF.inc
shm_alloc.o:		shm_alloc.c			shm_VDIF.inc
shm_init.o:			shm_init.c			shm_VDIF.inc
erase_shm.o:		erase_shm.c			shm_VDIF.inc
shm_param_view.o:	shm_param_view.c	shm_VDIF.inc
shm_access.o:		shm_access.c		shm_VDIF.inc
shm_init_create.o:	shm_init_create.c	shm_VDIF.inc
shm_spec_view.o:	shm_spec_view.c		shm_VDIF.inc
shm_power_view.o:	shm_power_view.c	shm_VDIF.inc
timesystem.o:		timesystem.c
vdif_head_extract.o:	vdif_head_extract.c	shm_VDIF.inc
cpg_setup.o:		cpg_setup.c
cpg_spec.o:			cpg_spec.c
cpg_power.o:		cpg_power.c

#----------------- End of File --------------------
