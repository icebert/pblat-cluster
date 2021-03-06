===================================================
pblat-cluster - parallelized blat (cluster version)
===================================================
blat with cluster parallel hybrid computing support
---------------------------------------------------

.. image:: https://circleci.com/gh/icebert/pblat-cluster.svg?style=shield
    :target: https://circleci.com/gh/icebert/pblat-cluster

When the query file format is fasta, you can specify many processes in a cluster
to process it. Processes running in the same node use shared memory. Thus pblat-cluster
works in hybrid computing mode. This will minimize the memory usage per node. It can
reduce run time linearly. This program is useful when you blat a big query file to a
huge reference like human whole genome sequence.

The program is based on the original blat program which was written by Jim Kent.

pblat-cluster can run on Linux clusters with MPI support.

----

Install
------------
To compile the source code, simply enter the source code directory in terminal
and issue the "make" command. When the compiling finished, the executable
pblat-cluster will locate in the same directory. Then it can be moved to where
you want.

By default the makefile will use mpicc to compile the codes. You can specify
other MPI compilers installed in your cluster. For example, using Intel MPI
compiler by typing "make CC=mpiicc" .


Run
------------
Two ways to run pblat-cluster in a cluster:

1) **without PBS**

::

  mpirun -n <N> pblat-cluster database query output.psl

2) **with PBS**

You can write a bash script and submit to PBS using qsub/sbatch.

* **qsub script example**
::

  #!/bin/bash
  #PBS -N pblat
  #PBS -l nodes=32:ppn=4
  
  cd workingdir
  
  mpirun pblat-cluster genome.fa reads.fa out.psl

* **sbatch script example**
::

  #!/bin/bash
  #SBATCH -J pblat
  #SBATCH -N 32
  #SBATCH -n 4
  
  cd workingdir
  
  mpirun pblat-cluster genome.fa reads.fa out.psl

----

Licence
------------
pblat is modified from blat, the licence is the same as blat. The source code and
executables are freely available for academic, nonprofit and personal use. Commercial
licensing information is available on the Kent Informatics website.

Cite
---------------
Wang M & Kong L. **pblat: a multithread blat algorithm speeding up aligning sequences
to genomes**. *BMC Bioinformatics* 2019, 20(1). `[full text]
<https://bmcbioinformatics.biomedcentral.com/articles/10.1186/s12859-019-2597-8>`_

----

Copyright (C) 2012 - 2020 Wang Meng

Contact me: wangm@mail.cbi.pku.edu.cn 
