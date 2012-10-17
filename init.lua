print("Dumping discovered methods in node 'urpc'")
urpc = { }

for i,j in pairs(_urpc) do
   print(i,j)
   urpc[j] = function(...)
      id=i;
      urpc_call(id,unpack(arg))
   end
end
print("Entering interactive shell, CTRL+D to exit")
