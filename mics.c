
unsigned createMask(unsigned from, unsigned to)
{
   unsigned mask = 0;
   unsigned i;
   for(i=from; i<=to; i++){
       mask |= 1 << i;
   }
   return mask;
}
