.globl _ZN12MMAPR3000Bus13read_mem_implIhEET_PVh
.globl _ZN12MMAPR3000Bus13read_mem_implItEET_PVh
.globl _ZN12MMAPR3000Bus13read_mem_implIjEET_PVh
.globl _ZN12MMAPR3000Bus14write_mem_implIhEEvPVhT_
.globl _ZN12MMAPR3000Bus14write_mem_implItEEvPVhT_
.globl _ZN12MMAPR3000Bus14write_mem_implIjEEvPVhT_

.section .text

// unsigned char MMAPR3000Bus::read_mem_impl<unsigned char>(unsigned char volatile*)
_ZN12MMAPR3000Bus13read_mem_implIhEET_PVh:
    movzbl (%rdi), %eax
    ret

// unsigned short MMAPR3000Bus::read_mem_impl<unsigned short>(unsigned char volatile*)
_ZN12MMAPR3000Bus13read_mem_implItEET_PVh:
    movzwl (%rdi), %eax
    ret

// unsigned int MMAPR3000Bus::read_mem_impl<unsigned int>(unsigned char volatile*)
_ZN12MMAPR3000Bus13read_mem_implIjEET_PVh:
    mov    (%rdi), %eax
    ret

// void MMAPR3000Bus::write_mem_impl<unsigned char>(unsigned char volatile*, unsigned char)
_ZN12MMAPR3000Bus14write_mem_implIhEEvPVhT_:
    mov    %dl, (%rdi)
    ret

// void MMAPR3000Bus::write_mem_impl<unsigned short>(unsigned char volatile*, unsigned short)
_ZN12MMAPR3000Bus14write_mem_implItEEvPVhT_:
    mov    %dx, (%rdi)
    ret

// void MMAPR3000Bus::write_mem_impl<unsigned int>(unsigned char volatile*, unsigned int)
_ZN12MMAPR3000Bus14write_mem_implIjEEvPVhT_:
    mov    %edx, (%rdi)
    ret
