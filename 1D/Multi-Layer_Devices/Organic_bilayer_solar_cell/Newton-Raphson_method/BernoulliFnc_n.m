function B_n = BernoulliFnc_n(fullV)
global num_cell phi_c Vt l gap_elec

dV = zeros(1,num_cell+1);
B_n = zeros(2, num_cell+1);

for i = 2:num_cell+1                 % so dV(100) = dV(101: is at x=L) - dV(100, at x= l-dx).
    dV(i) = fullV(i)-fullV(i-1);     % these fullV's = V/Vt, when solved poisson.
end

dV(num_cell+1) = dV(num_cell+1) + phi_c/Vt;          %injection step at right bndry (electrons electrode)
dV(l+1) = dV(l+1) + gap_elec/Vt;                     %energy step at interface

for i = 2:num_cell+1
    B_n(1,i) = dV(i)/(exp(dV(i))-1.0);    %B(+dV)
    B_n(2,i) = B_n(1,i)*exp(dV(i));       %B(-dV)
end
