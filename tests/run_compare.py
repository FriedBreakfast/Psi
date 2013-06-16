import subprocess
import sys
import difflib


def main():
  with open(sys.argv[1], 'rU') as expected_in:
    expected_lines = expected_in.readlines()
  
  child = subprocess.Popen(sys.argv[2:], stdout=subprocess.PIPE, universal_newlines=True)
  child_lines = child.stdout.readlines()
  if child.wait():
    print 'Child process failed with exit code %s' % child.returncode
    sys.exit(1)
    
  same = True
  for line in difflib.context_diff(expected_lines, child_lines, 'expected', 'actual'):
    same = False
    sys.stdout.write(line)
  
  sys.exit(0 if same else 1)


if __name__ == '__main__':
  main()
