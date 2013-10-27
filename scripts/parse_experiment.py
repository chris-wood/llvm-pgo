import sys

def usage():
    print >> sys.stderr, "usage: python parse_experiment.py list_of_program_prefixes"

def main():
    print >> sys.stderr, "Starting to parse output..."
    data = {} # map to list of lists
    fname = sys.argv[1]
    prefix_fnames = open(fname, 'r')
    for prefix in prefix_fnames:
        prefix = prefix.strip()
        print >> sys.stderr, "Parsing: " + str(prefix)
        
        # walk all files that start with the prefix... if they contain .mod then add time in each file to the modified bucket, otherwise add time to the regular bucket

if __name__ == "__main__":
    main()
