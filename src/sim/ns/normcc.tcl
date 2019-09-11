
# This script exercises NORM-CC versus TCP flows over
# a single bottleneck topology.  There is one NORM flow
# and possibly multiple TCP flows.  The number of receivers
# in the NORM "group" can vary (default gsize:10)

proc normcc {optionList} {

# normcc usage
set usage {Usage: ns normcc.tcl [brate <bottleneckRate>][tcp <numTcp>][gsize <count>]}
append usage {[rate <sendRate>][duration <sec>][backoff <k>][trace]}

#Some default parameters for NORM
set groupSize "10"
set backoffFactor "4.0"
set sendRate "32kb"
set bottleneckRate "1Mb"
set duration "240.0"
set nsTracing 1
set numTcp 1
set queueType "DropTail"
set queueSize 100

#Parse optionList for parameters
set state flag
foreach option $optionList {
    switch -- $state {
        "flag" {
            switch -glob -- $option {
                "gsize" {set state "gsize"}
                "rate" {set state "rate"}
                "brate" {set state "brate"}
                "backoff" {set state "backoff"}
                "duration" {set state "duration"}
                "tcp" {set state "tcp"}
                "red" {set queueType "RED"}
                "trace" {set nsTracing 1}
                default {
                    puts "normcc: Bad option $option"
                    puts "$usage"
                    exit
                }
            }
        
        }
        "gsize" {
            set groupSize $option
            set state "flag"
        }
        "backoff" {
            set backoffFactor $option
            set state "flag"
        }
        "brate" {
            set bottleneckRate $option
            set state "flag"
        }
        "rate" {
            set sendRate $option
            set state "flag"
        }
        "duration" {
            set duration $option
            set state "flag"
        }
        "tcp" {
            set numTcp $option
            set state "flag"
        }
        default {
            error "normcc: Bad option parse state!"
        }
    }
}


# 1) An ns-2 simulator instance is created an configured
#   for multicast operation with a dense mode (DM) multicast
#   routing protocol:
set ns_ [new Simulator -multicast on]
$ns_ multicast


# 2) Trace files are opened and ns and nam 
#    tracing is enabled:
if {$nsTracing} {
    set f [open normcc.tr w]
    $ns_ trace-all $f
    set nf [open normcc.nam w]
    $ns_ namtrace-all $nf
} else {
    set f 0
    set nf 0
}


# 3) Create a single bottleneck 
# Link 0 <-> 1 is our bottleneck
set n(0) [$ns_ node]
set n(1) [$ns_ node]
puts "normcc: Creating bottleneck link ..."
$ns_ duplex-link $n(0) $n(1) $bottleneckRate 10ms $queueType
$ns_ queue-limit $n(0) $n(1) $queueSize
$ns_ duplex-link-op $n(0) $n(1) queuePos 0.5

if {"RED" == $queueType} {
    set redq [[$ns_ link $n(0) $n(1)] queue]
    $redq set thresh_ [expr int($queueSize/10 + 0.9)]
    $redq set maxthresh_ [expr int($queueSize/2 +0.9)]
    $redq set linterm_ 10
}


# Non-bottleneck link rate is 10 * bottleneckRate
set linkRate [expr 10 * [bw_parse $bottleneckRate]]

# 4) Create a single NORM sender and link to bottleneck
puts "normcc: Creating NORM sender ..."
set n(2) [$ns_ node]
set norm_sender [new Agent/NORM]
$ns_ attach-agent $n(2) $norm_sender
# Link from NORM sender to bottleneck
$ns_ duplex-link $n(2) $n(0) $linkRate 1ms DropTail
$ns_ queue-limit $n(2) $n(0) 100
$ns_ duplex-link-op $n(2) $n(0) queuePos 0.5

# 5) Create nodes with TCP sources and links to bottleneck
if {$numTcp > 0} {
    puts "normcc: Creating TCP sources ..."
}
for {set i 0} {$i < $numTcp} {incr i} {
    set k [expr $i + 3]
    set n($k) [$ns_ node]
    set tcp_src($i) [new Agent/TCP/FullTcp]
    $ns_ attach-agent $n($k) $tcp_src($i)
    $tcp_src($i) set window_ 100
    $tcp_src($i) set packetSize_ 512
    # Links from TCP sources to bottleneck
    $ns_ duplex-link $n($k) $n(0) $linkRate 1ms DropTail
    $ns_ queue-limit $n($k) $n(0) 100
    $ns_ duplex-link-op $n($k) $n(0) queuePos 0.5
    # Attach FTP app to tcp sources
    set ftp($i) [new Application/FTP]
    $ftp($i) attach-agent $tcp_src($i)
}

# 6) Create nodes with NORM receivers and links to bottleneck
puts "normcc: Creating NORM receivers ..."
for {set i 0} {$i < $groupSize} {incr i} {
    set k [expr $i + 3 + $numTcp]
    set n($k) [$ns_ node]
    set norm_receiver($i) [new Agent/NORM]
    $ns_ attach-agent $n($k) $norm_receiver($i)
    # Links from bottleneck to NORM receivers
    $ns_ duplex-link $n(1) $n($k) $linkRate 1ms DropTail
    $ns_ queue-limit $n(1) $n($k) 100
    $ns_ duplex-link-op $n(1) $n($k) queuePos 0.5
}

# 7) Create nodes with TCP sinks and links to bottleneck
puts "normcc: Creating TCP sinks..."
for {set i 0} {$i < $numTcp} {incr i} {
    set k [expr $i + 4 + $numTcp + $groupSize]
    set n($k) [$ns_ node]
    set tcp_sink($i) [new Agent/TCP/FullTcp]
    $ns_ attach-agent $n($k) $tcp_sink($i)
    # Links from bottleneck to TCP sinks
    $ns_ duplex-link $n(1) $n($k) $linkRate 1ms DropTail
    $ns_ queue-limit $n(1) $n($k) 100
    $ns_ duplex-link-op $n(1) $n($k) queuePos 0.5
    # Connect tcp sources->sinks
    $ns_ connect $tcp_src($i) $tcp_sink($i)
    $tcp_sink($i) listen
    
}

# 8) Configure multicast routing for topology
set mproto DM
set mrthandle [$ns_ mrtproto $mproto  {}]
 if {$mrthandle != ""} {
     $mrthandle set_c_rp [list $n(1)]
}

# 9) Allocate a multicast address to use
set group [Node allocaddr]

puts "normcc: Configuring NORM agents ..."

# 10) Configure global NORM agent commands (using norm_sender)
$norm_sender debug 2
#$norm_sender log normLog.txt

# 11) Configure NORM sender parameters 
$norm_sender address $group/5000
$norm_sender rate [bw_parse $sendRate]
$norm_sender cc on
$norm_sender backoff $backoffFactor

$norm_sender segment 532
$norm_sender block  64
$norm_sender parity 32
$norm_sender txbuffer 1000000

$norm_sender repeat -1
$norm_sender interval 0.0
$norm_sender txloss 0.0
$norm_sender gsize $groupSize
#$norm_sender trace

# 12) Configure NORM receiver parameters
for {set i 0} {$i < $groupSize} {incr i} {
    $norm_receiver($i) address $group/5000
    $norm_receiver($i) backoff $backoffFactor
    $norm_receiver($i) rxbuffer 1000000
    $norm_receiver($i) txloss 0.0
    $norm_receiver($i) gsize $groupSize
    #$norm_receiver($i) trace
}

# 13) Start NORM sender and receivers
$ns_ at 0.0 "$norm_sender start sender"
for {set i 0} {$i < $groupSize} {incr i} {
    $ns_ at 0.0 "$norm_receiver($i) start receiver"
}
$ns_ at 0.0 "$norm_sender sendStream 1000000"

# 14) Start tcp flows
for {set i 0} {$i < $numTcp} {incr i} {
    $ns_ at 0.0 "$ftp($i) start"
}

# 15) Schedule finish time
$ns_ at $duration "finish $ns_ $f $nf $nsTracing"

puts "normcc: Running simulation (tcp:$numTcp gsize:$groupSize rate:$sendRate backoff $backoffFactor duration:$duration) ..."
$ns_ run

}

proc finish {ns_ f nf nsTracing} {
    puts "normcc: Done."
    $ns_ flush-trace
    if {$nsTracing} {
	    close $f
	    close $nf
    }
    $ns_ halt
    delete $ns_
}

# Run a set of trials with optional command-line parameters
puts "Running normcc: $argv"
normcc $argv

