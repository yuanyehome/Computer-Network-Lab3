set terminal png transparent enhanced font "arial,10" fontscale 1.0 size 600, 400 
set output 'gnuplot.png'
set style increment default
set style data lines
set xlabel 'time elapsed (ms)'
set ylabel 'byte range (Kb)'
plot [0:40000][0:40000] "data1.txt" title '1' with points, "data2.txt" title '2' with points, "data3.txt" title '3' with points
