NAR0 = 1.31/0.73
EyalGluNAR(NAR = 1.8, τd = 35ms) = Glutamatergic(
    Receptor(E_rev = 0.0, τr = 0.25, τd = 2.0, g0 = 0.73(1+NAR0-NAR)),
    ReceptorVoltage(E_rev = 0.0, τr = 8, τd = τd, g0 = 0.73*NAR, nmda = 1.0f0),
)

EyalEquivalentNAR(NAR, τd = 35) = Receptors(EyalGluNAR(NAR, τd), MilesGabaDend)

quaresima2022_nar(nar, τ = 35ms) = (
    dends = [(150um, 400um), (150um, 400um)],
    soma_syn = Receptors(DuarteGluSoma, MilesGabaSoma), # defines glutamaterbic and gabaergic receptors in the soma
    dend_syn = EyalEquivalentNAR(nar, τ), # defines glutamaterbic and gabaergic receptors in the dendrites
    NMDA = EyalNMDA, # NMDA synapse
    param = AdExSoma(Vr = -55mV, Vt = -50mV),
)


export EyalEquivalentNAR, quaresima2022_nonmda, quaresima2022_nar
