@echo off

python generate.py > temp.json
scp temp.json clong4947@login.hpc.lab:/home/clong4947/projects/metis/temp.json
ssh clong4947@login.hpc.lab "mpirun -np 4 /home/clong4947/projects/metis/bin/x64/Debug/metis.out /home/clong4947/projects/metis/temp.json" > output.sim
python visualize.py temp.json output.sim