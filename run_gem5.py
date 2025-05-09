import os
import sys
import subprocess
import shutil
import argparse
from datetime import datetime

def parse_comma_separated(arg_string):
    """Parses a comma-separated string into a list."""
    return [item.strip() for item in arg_string.split(',')]

if __name__ == "__main__":

    parser = argparse.ArgumentParser()

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    parser.add_argument(
        "positional_args",
        help="Positional arguments which will form the basis of our gem5 command itself",
        nargs="*"
    )
    parser.add_argument(
        "--scratchspace",
        help="Directory to place the output files into",
        default=os.path.curdir
    )
    parser.add_argument(
        "--gem5",
        help="Directory to place the output files into",
        default="./build/X86/gem5.opt"
    )
    parser.add_argument(
        "--config",
        help="Gem5 config file to use",
        default="/data2/sumanthu/gem5/configs/example/tutorial/multi_core_checkpoint.py"
    )
    parser.add_argument(
        "--copy",
        type=parse_comma_separated,
        help="Files to copy from scratchspace into the working directory",
        default=None
    )

    options = parser.parse_args()

    #Create the working directory
    working_dir = os.path.abspath(os.path.join(options.scratchspace,f"gem5_run_{timestamp}"))
    os.makedirs(working_dir)
    #Copy the gem5 binary
    shutil.copy(os.path.join(options.gem5),os.path.join(working_dir,"gem5_bin"))
    #Copy the additional files as well
    if options.copy != None:
        for f in options.copy:
            shutil.copy(os.path.join(options.scratchspace,f),working_dir)

    # Move into working directory
    os.chdir(working_dir)


    print(options.copy)

    #Command
    cmd = ' '.join(options.positional_args)

    #Run the command
    gem5_cmd = f"./gem5_bin {options.config} {cmd}"
    print(gem5_cmd)
    subprocess.run(gem5_cmd,shell=True)

    # #Delete the gem5 binary
    os.remove('gem5_bin')
    