import os
import sys
import argparse
import subprocess
import multiprocessing

def run_command(cmd:str):
    print(cmd)
    subprocess.run(cmd,shell=True)

if __name__ == "__main__":
    
    parser = argparse.ArgumentParser(description="Process arguments.")

    parser.add_argument("optional_arg", nargs="?", help="An optional positional argument")
    parser.add_argument("-n", help="Number of parallel workers per build", type=int, default=16)
    parser.add_argument("-p", help="Number of parallel builds", type=int, default=1)

    args = parser.parse_args()
    
    possible_opts = ["opt","debug","fast"]
    
    opts = []
    
    cwd = os.path.abspath(os.path.curdir)
    build_path = os.path.join(cwd,"build/X86")
    if args.optional_arg == None:
        opts = possible_opts
    else:
        for opt in args.optional_args:
            if opt not in possible_opts:
                print(f"{opt} not among {possible_opts}")
                exit(1)
            opts.append(opt)
    
    embedded_text = r"""#!/usr/bin/expect -f

set build_type [lindex $argv 0]
spawn scons build/X86/gem5.$build_type -j -num_cores-

puts "build_type: $build_type"
expect "Press enter to continue, or ctrl-c to abort:" {
	send "\r"
	expect "It is strongly recommended you install the pre-commit hooks before working with gem5. Do you want to continue compilation (y/n)?" {
		send "y\r"
		expect -timeout 3600 "scons: done building targets." {
			puts "$expect_out(buffer)"
		} timeout {
            puts "timed out waiting for build to finish after 3600s. Consider increasing the period"
        }
    }
}
"""

    #Replace number of cores with n
    embedded_text = embedded_text.replace(r"-num_cores-",f"{args.n}")
    
    # # Create an expect script
    # lines = [f"#!/usr/bin/expect -f\n",
    #          f"\n"
    #          f"set build_type [lindex $argv 0]"
    #          f"\n"
    #          f"spawn scons build/X86/gem5.$build_type -j {args.n}\n",
    #          f"\n"
    #          f"puts \"build_type: $build_type\""
    #          f"\n"
    #          f"expect \"Press enter to continue, or ctrl-c to abort:\" {{\n",
    #          f"\tsend \"\\r\"\n",
    #          f"\texpect \"It is strongly recommended you install the pre-commit hooks before working with gem5. Do you want to continue compilation (y/n)?\" {{\n"
    #          f"\t\tsend \"y\\r\"\n"
    #          f"\t\texpect \"scons: done building targets\" {{\n",
    #          f"\t\t\tputs \"$expect_out(buffer)\"\n",
    #          f"\t\t}}\n",
    #          f"\t}} timeout {{\n",
    #          f"\t\tputs \"Timeout occurred waiting for second prompt.\"\n",
    #          f"\t\texit 1\n",
    #          f"\t}}\n",
    #          f"}}\n"]
    with open("launch.exp","w") as file:
        file.write(embedded_text)
        
    # Set the executable permission on the file
    os.chmod("launch.exp",0o755)
    
    cmds = []
    
    for opt in opts:
        cmds.append((f"./launch.exp {opt}",))
    
    print(cmds)
    
    # Launch the processes
    with multiprocessing.Pool(processes=args.p) as Pool:
        Pool.starmap(run_command,cmds)
        
    