
# This script describes and illustrates the commands and usage of the
# NORM ns-2 simulation agent.  

# 1) An ns-2 simulator instance is created an configured
#   for multicast operation with a dense mode (DM) multicast
#   routing protocol:
set ns_ [new Simulator -multicast on]
$ns_ multicast


# 2) Trace files are opened and ns and nam 
#    tracing is enabled:
set f [open example.tr w]
$ns_ trace-all $f
set nf [open example.nam w]
$ns_ namtrace-all $nf

set numNodes 10

# 3) A simple hub and spoke topology is created with
#    a NORM agent attached to each node

# Node 0 is the hub
set n(0) [$ns_ node]

puts "Creating nodes and norm agents ..."
for {set i 1} {$i <= $numNodes} {incr i} {
    set n($i) [$ns_ node]    
    set norm($i) [new Agent/NORM]
    $ns_ attach-agent $n($i) $norm($i)
}

puts "Creating topology ..."
for {set i 1} {$i <= $numNodes} {incr i} {
    $ns_ duplex-link $n(0) $n($i) 1Mb 1ms DropTail
    $ns_ queue-limit $n(0) $n($i) 100
    #$ns_ duplex-link-op $n(0) $n($i) orient right
    $ns_ duplex-link-op $n(0) $n($i) queuePos 0.5
}

# 4) Configure multicast routing as needed
set mproto DM
set mrthandle [$ns_ mrtproto $mproto  {}]
if {$mrthandle != ""} {
     $mrthandle set_c_rp [list $n(0)]
}

# 4) Allocate a multicast address to use
set group [Node allocaddr]

puts "Configuring NORM agents ..."

# 5) Configure global NORM agent parameters (debugging/logging is global)
#    (Uncomment the "log" command to direct NORM debug output to a file)
$norm(1) debug 2
$norm(1) log normLog.txt

# 6) Configure NORM server agent at node 1
$norm(1) address $group/1
$norm(1) rate 32000
$norm(1) block 1
$norm(1) parity 0
$norm(1) repeat -1
$norm(1) interval 0.0
$norm(1) txloss 0.0
# Enabl NORM message tracing at the server
#$norm(1) trace

# 7) Configure NORM client agents at other nodes
for {set i 2} {$i <= $numNodes} {incr i} {
    $norm($i) address $group/1
    $norm($i) rxbuffer 100000
    $norm($i) rxloss 0.0
    #$norm($i) trace
}

# 8) Start server and clients
$ns_ at 0.0 "$norm(1) start server"
for {set i 2} {$i <= $numNodes} {incr i} {
    $ns_ at 0.0 "$norm($i) start client"
}
$ns_ at 0.0 "$norm(1) sendFile 64000"

$ns_ at 300.0 "finish $ns_ $f $nf"

proc finish {ns_ f nf} {
    $ns_ flush-trace
	close $f
	close $nf
    $ns_ halt
    delete $ns_
}

$ns_ run

