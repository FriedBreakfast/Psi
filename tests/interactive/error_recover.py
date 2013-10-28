from psi_interact import test_start

with test_start() as p:
  p.check('libc : library{}')
  p.check('puts_type : function (::pointer(ubyte) -: int)')
  p.check('puts : libc.symbol (puts_type) {}')
  p.check_fail('puts({hello})')
  p.check('puts_alt : libc.symbol (puts_type) {"type":"c","name":"puts"}')
  p.check('puts_alt({hello})', 'hello\n')
