# Precision-Time-Protocol
Barebone Implementation of Precision Time Protocol in C - For Embedded Applications

- The same code can be put on Slave Processors and Master Processors, an outside or driving program can select Master or Slave and send the Slave IP's to the Master processor. 
- getOffset(..) can be called to find the final offset.
- To calculate corrected time: abs(MasterTime - Offset - SlaveTime) = Delta Ticks
