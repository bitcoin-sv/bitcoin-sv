import multiprocessing
def nproc():
    cores =  multiprocessing.cpu_count()
    nproc =  "-j" + str(cores)
    return(nproc)

