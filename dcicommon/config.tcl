#
# config.tcl --
#
#	Support for av-based config.  Usage:
#
#	To build a config:
#
#		% bin/nsd -t config.tcl nsd.tcl build
#
#	To run the most recent config:
#
#		bin/nsd -t config.tcl nsd.tcl
#


#	
#
# config.wrap --
#
#	Load the nsd.tcl and reset Tcl init script to be this script
#	and, when building a config, disable all binary module loads.
#

proc config.wrap {nsdtcl build} {
	#
	# Source the original config script.
	#
	source $nsdtcl

	#
	# Save the build variable for use during server init.
	#
	ns_section dci/config
	ns_param build $build

	#
	# Update all virtual servers.
	#
	set servers [ns_configsection ns/servers]
	for {set i 0} {$i < [ns_set size $servers]} {incr i} {
		set server [ns_set key $servers $i]

		#
		# Set the name of the config av for use by
		# the av module init C code.
		#
		set avpath ns/server/$server/module/dci/av
		ns_section $avpath
		ns_param config config_$server
		ns_set print [ns_configsection $avpath]

		#
		# Set this script as the server Tcl init.
		#
		set tcl [ns_configsection ns/server/$server/tcl]
		set idx [ns_set ifind $tcl initfile]
		if {$idx >= 0} {
			ns_set delete $tcl $idx
		}
		ns_set put $tcl initfile [ns_info config]
		ns_set print $tcl

		#
		# Set all modules to Tcl-only on build.
		#
		if $build {
			set modules [ns_configsection ns/server/$server/modules]
			set mods ""
			for {set j 0} {$j < [ns_set size $modules]} {incr j} {
				lappend mods [ns_set key $modules $j]
			}
			ns_set trunc $modules 0
			foreach mod $mods {
				ns_set put $modules $mod tcl
			}
			ns_set put $tcl initfile [ns_info home]/config/config.build.tcl
			ns_set print $modules
		}
	}
}


#
# config.run --
#
#	Run an av-based config.
#

proc config.run {} {
	set config [av.config]
	if {$config != ""} {
		set dir [ns_info home]/config
		set version current
		set file $dir/$version/config.av
		if [file exists $file] {
			set avfile [av.file $config]
			file delete $avfile
			file link -symbolic $avfile $file
			set init [av.mget $config init]
			if {$init != ""} {
				av.unknown $config
				eval $init
			}
		}
	}
	ns_markfordelete
}


#
# config.build --
#
#	Build an av-based config.
#

proc config.build {} {
	#
	# When building, all actual module loads are disabled so load libdci now
	# to get the dci.getprocs and av commands.
	#

	load [ns_info home]/lib/libdci[info sharedlib]

	#
	# Create a new versioned config dir.
	# 

	set version [ns_time]
	set confdir [ns_info home]/config
	set config $confdir/$version
	file mkdir $config
	set link $confdir/current

	#
	# Vars to accumulate init commands and proc defs.
	#

	set procs ""
	set init ""

	#
	# Get core aolserver procs but ignore the init commands.
	#

	config.getprocs [ns_info home]/bin/init.tcl init procs
	set init ""

	#
	# Get shared and private module-independent scripts.
	#

	foreach lib {shared private} {
		foreach file [glob -nocomplain [ns_library $lib]/*.tcl] {
			config.getprocs $file init procs
		}
	}

	#
	# Get module specific scripts.  
	#

	foreach mod [ns_ictl getmodules] {
		set shared [ns_library shared $mod]
		set private [ns_library private $mod]

		#
		# Append the ns_module calls as with file-based config.
		#

		append init "ns_module name $mod\n"
		append init "ns_module private $private\n"
		append init "ns_module shared $shared\n"
		foreach dir [list $private $shared] {
			foreach file [glob -nocomplain $dir/*.tcl] {
				config.getprocs $file init procs
			}
		}
		append init "ns_module clear\n"
	}

	#
	# Add procs to disable ns_eval and bootstrap ns_init via config.once
	# invoked from the AvConfigInit C-level interp init callback in av.c
	#

	append procs {
		# BEGIN: config build additions
		proc ns_eval args {
			error "disabled - update av-based config."
		}
		proc config.once {} {
			set body {av.cleanup; av.unknown [av.config]}
			set init [av.get [av.config] ns_init]
			if {$init != ""} {
				eval $init
				rename ns_init _ns_init
				append body "; _ns_init"
			}
			proc ns_init {} $body
			ns_init
		}
		# END
	}

	#
	# Dump the parsed results.
	#

	dci.writefile $config/procs.out $procs
	dci.writefile $config/init.out $init

	#
	# Create a safe interp to turn the procs into an av-style proc list.
	#

	set i [interp create -safe]
	interp eval $i {foreach p [info proc] {rename $p ""}}
	interp eval $i $procs
	set plist [interp eval $i {
		set plist ""
		foreach p [info proc] {
			set args ""
			foreach a [info args $p] {
				if [info default $p $a d] {
					lappend args [list $a $d]
				} else {
					lappend args $a
				}
			}
			lappend plist $p [list proc $p $args [info body $p]]
		}
		set plist
	}]
	interp delete $i

	#
	# Dump the plist and create the av.
	#
	
	dci.writefile $config/plist.out $plist
	av.create $config/config.av $plist [list init $init version $version]

	file delete $link
	file link -symbolic $link $confdir/$version

	#
	# Exit the server now.
	#

	exit
}

#
# config.getprocs --
#
#	Parse procs and init commands from a file.
#

proc config.getprocs {file initVar procsVar} {
	upvar $initVar init
	upvar $procsVar procs
	set fp [open $file]
	dci.getprocs [read $fp] init procs
	close $fp
}



#
# Determine mode of operation, nsd.tcl wrap or server build or run.
#

if [info exists argv] {
	set nsdtcl [lindex $argv $optind]
	if {[lindex $argv [expr $optind + 1]] == "build"} {
		set build 1
	} else {
		set build 0
	}
	config.wrap $nsdtcl $build
} else {
	if [ns_config dci/config build] {
		config.build
	} else {
		config.run
	}
}
