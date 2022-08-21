
# This script describes and illustrates the commands and usage of the
# NORM ns-2 simulation agent.  

proc SimpleNORM {optionList} {

#Some default parameters for NORM
set groupSize "10"
set backoffFactor "4.0"
set sendRate "64kb"
set duration "120.0"
set nsTracing 0
set unicastNacks "off"
set cc "off"

#Parse optionList for parameters
set state "flag"
foreach option $optionList {
    if {"flag" != $state} {
        set reset true
    } else {
        set reset false
    } 
    switch -- $state {
        "flag" {
            switch -glob -- $option {
                "unicast" {set unicastNacks "on"}
                "cc" {set cc "on"}
                "trace" {set nsTracing 1}
                default {set state $option}
            }
        
        }
        "gsize" {
            set groupSize $option
        }
        "backoff" {
            set backoffFactor $option
        }
        "rate" {
            set sendRate $option
        }
        "duration" {
            set duration $option
        }
        default {
            error "simplenorm: bad option: $state"
        }
    }
    if {$reset == "true"} {set state "flag"}
}


# 1) An ns-2 simulator instance is created an configured
#   for multicast operation with a dense mode (DM) multicast
#   routing protocol:
set ns_ [new Simulator -multicast on]
$ns_ multicast


# 2) Trace files are opened and ns and nam 
#    tracing is enabled:
if {$nsTracing} {
    set f [open simplenorm.tr w]
    $ns_ trace-all $f
    set nf [open simplenorm.nam w]
    $ns_ namtrace-all $nf
} else {
    set f 0
    set nf 0
}

set numNodes [expr $groupSize + 1]

# 3) A simple hub and spoke topology is created with
#    a NORM agent attached to each spoke node

# Node 0 is the hub of our hub & spoke topology
# Note there is _not_ a NORM agent at the hub.
set n(0) [$ns_ node]

puts "simplenorm: Creating $numNodes nodes with norm agents ..."
for {set i 1} {$i <= $numNodes} {incr i} {
    set n($i) [$ns_ node]    
    set norm($i) [new Agent/NORM]
    $ns_ attach-agent $n($i) $norm($i)
}

puts "simplenorm: Creating spoke links ..."
set linkRate [expr $groupSize * [bw_parse $sendRate]]
puts "simplenorm: linkRate = [expr $linkRate / 1000.0] kbps"
for {set i 1} {$i <= $numNodes} {incr i} {
    $ns_ duplex-link $n(0) $n($i) $linkRate 100ms DropTail
    $ns_ queue-limit $n(0) $n($i) 100
    #$ns_ duplex-link-op $n(0) $n($i) orient right
    $ns_ duplex-link-op $n(0) $n($i) queuePos 0.5
}

# 4) Configure multicast routing for topology
set mproto DM
set mrthandle [$ns_ mrtproto $mproto  {}]
if {$mrthandle != ""} {
     $mrthandle set_c_rp [list $n(0)]
}

# 5) Allocate a multicast address to use
set group [Node allocaddr]

puts "simplenorm: Configuring NORM agents ..."

# 6) Configure global NORM agent commands (using norm(1))
$norm(1) debug 2
#$norm(1) log normLog.txt

# 7) Configure NORM sender agent at node 1
$norm(1) address $group/5000
$norm(1) rate [bw_parse $sendRate]
$norm(1) backoff $backoffFactor
$norm(1) parity 0
$norm(1) repeat 50
$norm(1) interval 0.0
$norm(1) txloss 10.0
$norm(1) gsize $groupSize
$norm(1) cc on
#$norm(1) trace on

# 8) Configure NORM receiver agents at other nodes
for {set i 2} {$i <= $numNodes} {incr i} {
    $norm($i) address $group/5000
    $norm($i) backoff $backoffFactor
    $norm($i) rxbuffer 1000000
    #$norm($i) txloss 10.0
    $norm($i) gsize $groupSize
    $norm($i) unicastNacks $unicastNacks
    #$norm($i) trace
}

# 9) Start sender and receivers
$ns_ at 0.0 "$norm(1) start sender"
for {set i 2} {$i <= $numNodes} {incr i} {
    $ns_ at 0.0 "$norm($i) start receiver"
}

$ns_ at 0.0 "$norm(1) sendFile 1000000"

$ns_ at $duration "finish $ns_ $f $nf $nsTracing"

puts "simplenorm: Running simulation (gsize:$groupSize rate:$sendRate backoff $backoffFactor duration:$duration) ..."
$ns_ run

}

proc finish {ns_ f nf nsTracing} {
    puts "simplenorm: Done."
    $ns_ flush-trace
    if {$nsTracing} {
	    close $f
	    close $nf
    }
    $ns_ halt
    delete $ns_
}

# Run a set of trials with optional command-line parameters

#Usage: 
#ns simplenorm.tcl [gsize <count>][rate <bps>][backoff <k>][duration <sec>][trace]

puts "Running simplenorm: $argv"
SimpleNORM $argv

