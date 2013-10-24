from psi_interact import test_start

with test_start() as p:
  p.check('libc : library{};')
  p.check('puts : libc.symbol (function_type (::pointer(ubyte) -: int)) {"type":"c","name":"puts"};')
  p.check('x : {hello};')
  p.check('puts(x);', 'hello\n')
