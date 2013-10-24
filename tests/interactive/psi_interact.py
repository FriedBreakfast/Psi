import subprocess


class PsiInterpreterError(Exception):
  pass


class PsiInterpreter(object):
  '''
  Class for interacting with the Psi interpreter.
  
  It is rather brittle because it requires the user to provide
  correctly formatted input to ensure that I/O deadlocks do not
  occur.
  
  It also requires that programs run through this interpreter
  only output to stdout not stderr, since anything on stderr
  is assumed to be an error message.
  '''
  
  def __init__(self, path, *args):
    '''
    Path: path to Psi interpreter
    '''
    self._child = subprocess.Popen([path, '--testprompt'] + list(args), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
  def close(self):
    self._child.stdin.close()
    self._child.stdout.close()
    self._child.stderr.close()
    self._child.wait()
    
  def __enter__(self):
    return self
  
  def __exit__(self, type, value, tb):
    self.close()
    
  def _read_to_null(self, fd):
    'Read until NULL byte is encountered'
    data = []
    while True:
      c = fd.read(1)
      if c == '\0':
        return ''.join(data)
      elif c == '':
        raise PsiInterpreterError('Unexpected end-of-stream')
      data.append(c)
  
  def run(self, cmd):
    '''
    Run a command through the interpreter.
    The command text is expected to be formatted appropriately for the interpreter
    and should form a single command entry.
    It should not end in a newline, this is added by run.
    
    Returns a pair (output,error)
    '''
    print >> self._child.stdin, cmd
    return (self._read_to_null(self._child.stdout), self._read_to_null(self._child.stderr))
  
  def check(self, cmd, output=''):
    '''
    Run a command, and raise an exception if it produces an error.
    
    Returns the normal output.
    '''
    out, err = self.run(cmd)
    if err:
      raise PsiInterpreterError('Psi interpreter error: %s' % err)
    if out != output:
      raise PsiInterpreterError('Wrong interpreter output: "%s" != "%s"' % (out, output))
    
  def check_fail(self, cmd):
    '''
    Run a command, and raise an exception if it does not produce an error
    
    Returns error output.
    '''
    out, err = self.run(cmd)
    if not err:
      raise PsiInterpreterError('Psi interpreter did not emit the expected error, instead got: %s' % out)
    return err



def test_start():
  '''
  Parse command line arguments for an interactive session test
  and return the resulting interpreter.
  '''
  import sys
  return PsiInterpreter(sys.argv[1])
