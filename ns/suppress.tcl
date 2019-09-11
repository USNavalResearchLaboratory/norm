#!/usr/bin/tclsh

# Runs ns simplenorm.tcl for Nack stats vs. group size

set sizeList {10 20 35 50 75 100 150 250 350 500 750 1000}

set rate 32kb
set duration 120.0
set backoff 4.0

set fileName "results.gp"

exec rm -f $fileName

# Gnuplot header
set outFile [open $fileName "w"]
puts $outFile "set title 'NACK Suppression Performance'"
puts $outFile "set data style lines"
puts $outFile "set xlabel 'Number of Receivers'"
puts $outFile "set ylabel 'NACK Transmission Fraction'"
puts $outFile "plot \\"
puts $outFile "'-' using 1:2 \\"
puts $outFile "'Receivers:%lf alpha:%lf' t 'Theoretical Multicast Nacking', \\"
puts $outFile "'-' using 1:2 \\"
puts $outFile "'Receivers:%lf Sent:%lf Suppressed:%lf alpha:%lf' t 'Measured Multicast Nacking'\n"
close $outFile

puts "Computing theoretical results ..."
# T = number of GRTT for NACK backoff timers ...
set T $backoff

# Theoretical Multicast NACK results
set outFile [open $fileName "a"]
puts $outFile "#Theoretical Multicast NACK Transmission Fraction"
foreach groupSize $sizeList {
    set lambda [expr log($groupSize) + 1.0]
    set N [expr exp((1.2/(2.0 * $T)) * ($lambda))]
    set alpha [expr $N / $groupSize]
    puts $outFile "Receivers:$groupSize alpha:$alpha" 
}
puts $outFile "e\n"
close $outFile

# Measured Multicast NACK results
set outFile [open $fileName "a"]
puts $outFile "#Measured Multicast NACK Transmission Fraction"
close $outFile 
foreach groupSize $sizeList {
    puts "Starting simulation run for groupSize:$groupSize ..."
    puts "   ns simplenorm.tcl gsize $groupSize backoff $backoff rate $rate duration $duration"
    catch {eval exec ns simplenorm.tcl gsize $groupSize backoff $backoff rate $rate duration $duration}
    puts "   cat normLog.txt | nc >> $fileName"        
    catch {eval exec cat normLog.txt | nc >> $fileName}
}
puts "Finished."
