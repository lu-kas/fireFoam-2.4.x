/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2010-2012 OpenCFD Ltd.
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM; if not, write to the Free Software Foundation,
    Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

\*---------------------------------------------------------------------------*/

#include "turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField.H"
#include "addToRunTimeSelectionTable.H"
#include "fvPatchFieldMapper.H"
#include "volFields.H"
#include "mappedPatchBase.H"
#include "mapDistribute.H"
#include "basicThermo.H"
#include "LESModel.H"

#include "constants.H"

#include "radiationModel.H"
#include "absorptionEmissionModel.H"


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace compressible
{

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField::
turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField
(
    const fvPatch& p,
    const DimensionedField<scalar, volMesh>& iF
)
:
    mixedFvPatchScalarField(p, iF),
    temperatureCoupledBase(patch(), "undefined", "undefined", "undefined-K"),
    radiationCoupledBase(p, "undefined", scalarField::null()),
    neighbourFieldName_("undefined-neighbourFieldName"),
    neighbourFieldRadiativeName_("undefined-neigbourFieldRadiativeName")
{
    this->refValue() = 0.0;
    this->refGrad() = 0.0;
    this->valueFraction() = 1.0;
}


turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField::
turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField
(
    const turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField& psf,
    const fvPatch& p,
    const DimensionedField<scalar, volMesh>& iF,
    const fvPatchFieldMapper& mapper
)
:
    mixedFvPatchScalarField(psf, p, iF, mapper),
    temperatureCoupledBase(patch(), psf),    
    radiationCoupledBase
    (
        p,
        psf.emissivityMethod(),
        psf.emissivity_
    ),
    neighbourFieldName_(psf.neighbourFieldName_),
    neighbourFieldRadiativeName_(psf.neighbourFieldRadiativeName_)
{}


turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField::
turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField
(
    const fvPatch& p,
    const DimensionedField<scalar, volMesh>& iF,
    const dictionary& dict
)
:
    mixedFvPatchScalarField(p, iF),
    temperatureCoupledBase(patch(), dict),    
    radiationCoupledBase(p, dict),
    neighbourFieldName_(dict.lookup("neighbourFieldName")),
    neighbourFieldRadiativeName_(dict.lookup("neighbourFieldRadiativeName"))
{
    if (!isA<mappedPatchBase>(this->patch().patch()))
    {
        FatalErrorIn
        (
            "turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField::"
            "turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField\n"
            "(\n"
            "    const fvPatch& p,\n"
            "    const DimensionedField<scalar, volMesh>& iF,\n"
            "    const dictionary& dict\n"
            ")\n"
        )   << "\n    patch type '" << p.type()
            << "' not type '" << mappedPatchBase::typeName << "'"
            << "\n    for patch " << p.name()
            << " of field " << dimensionedInternalField().name()
            << " in file " << dimensionedInternalField().objectPath()
            << exit(FatalError);
    }

    fvPatchScalarField::operator=(scalarField("value", dict, p.size()));

    if (dict.found("refValue"))
    {
        // Full restart
        refValue() = scalarField("refValue", dict, p.size());
        refGrad() = scalarField("refGradient", dict, p.size());
        valueFraction() = scalarField("valueFraction", dict, p.size());
    }
    else
    {
        // Start from user entered data. Assume fixedValue.
        refValue() = *this;
        refGrad() = 0.0;
        valueFraction() = 1.0;
    }
}


turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField::
turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField
(
    const turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField&
        wtcsf,
    const DimensionedField<scalar, volMesh>& iF
)
:
    mixedFvPatchScalarField(wtcsf, iF),
    temperatureCoupledBase(patch(), wtcsf),  
    radiationCoupledBase
    (
        wtcsf.patch(),
        wtcsf.emissivityMethod(),
        wtcsf.emissivity_
    ),
    neighbourFieldName_(wtcsf.neighbourFieldName_),
    neighbourFieldRadiativeName_(wtcsf.neighbourFieldRadiativeName_)
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField::
updateCoeffs()
{
    if (updated())
    {
        return;
    }

    // Get the coupling information from the mappedPatchBase
    const mappedPatchBase& mpp = refCast<const mappedPatchBase>
    (
        patch().patch()
    );
    const polyMesh& nbrMesh = mpp.sampleMesh();
    const fvPatch& nbrPatch = refCast<const fvMesh>
    (
        nbrMesh
    ).boundary()[mpp.samplePolyPatch().index()];

    scalarField intFld(patchInternalField());

    const turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField&
        nbrField =
        refCast
        <
            const turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField
        >
        (
            nbrPatch.lookupPatchField<volScalarField, scalar>
            (
                neighbourFieldName_
            )
        );

    // Swap to obtain full local values of neighbour internal field
    scalarField nbrIntFld(nbrField.patchInternalField());
    mpp.distribute(nbrIntFld);

    // Swap to obtain full local values of neighbour K*delta
    scalarField nbrKDelta(nbrField.kappa(nbrField)*nbrPatch.deltaCoeffs());
    
    mpp.distribute(nbrKDelta);


    //scalarField nbrConvFlux = nbrKDelta*(*this - nbrIntFld);
    scalarField nbrConvFlux(nbrKDelta*(intFld - nbrIntFld));

    scalarField nbrTotalFlux = nbrConvFlux;

    scalarList radField(nbrPatch.size(),0.0);  
    scalarField Twall(patch().size(),0.0);

    // In solid
    if(neighbourFieldRadiativeName_ != "none") //nbr Radiation Qr
    {
        radField =
            nbrPatch.lookupPatchField<volScalarField, scalar>
            (
                neighbourFieldRadiativeName_
            );

        // Swap to obtain full local values of neighbour radiative heat flux field
        mpp.distribute(radField);

            const fvMesh& mesh = patch().boundaryMesh().mesh();
            if (! (mesh.foundObject<radiation::radiationModel>("radiationProperties")))
            {
                FatalErrorIn
                (
                    "turbulentTemperatureRadiationCoupledMixedSTFvPatchScalarField::"
                    "turbulentTemperatureRadiationCoupledMixedSTFvPatchScalarField\n"
                    "(\n"
                    "    const fvPatch& p,\n"
                    "    const DimensionedField<scalar, volMesh>& iF,\n"
                    "    const dictionary& dict\n"
                    ")\n"
                )   << "\n    radiationProperties file not found in pyrolysis region\n" 
                    << exit(FatalError);
            }
            const radiation::radiationModel& radiation =
                mesh.lookupObject<radiation::radiationModel>
                (
                    "radiationProperties"
                );

            scalarField temissivity
            (
                radiation.absorptionEmission().e()().boundaryField()
                [
                    //nbrPatch.index()
                    patch().index()
                ]
            );
            

        nbrTotalFlux -= radField*temissivity;
        nbrTotalFlux += temissivity*constant::physicoChemical::sigma.value()*pow(*this,4);

        //this->refValue() = operator[];  // not used
        this->refValue() = 0.0;  // not used
        this->refGrad() = -nbrTotalFlux/kappa(*this);                
        this->valueFraction() = 0.0;
    }
    else // In fluid
    {
        Twall = nbrIntFld;

        this->refValue() = Twall;
        this->refGrad() = 0.0;   // not used
        this->valueFraction() = 1.0;
    }

    mixedFvPatchScalarField::updateCoeffs();

    if (debug)
    {
        scalar Qc = gSum(nbrConvFlux*patch().magSf());
        scalar Qr = gSum(radField*patch().magSf());
        scalar Qt = gSum(nbrTotalFlux*patch().magSf());

        Info<< patch().boundaryMesh().mesh().name() << ':'
            << patch().name() << ':'
            << this->dimensionedInternalField().name() << " -> "
            << nbrMesh.name() << ':'
            << nbrPatch.name() << ':'
            << this->dimensionedInternalField().name() << " :"
            << " heatFlux:" << Qc
            << " radiativeFlux:" << Qr
            << " totalFlux:" << Qt
            << " walltemperature "
            << " min:" << gMin(*this)
            << " max:" << gMax(*this)
            << " avg:" << gAverage(*this)
            << endl;
    }
}


void turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField::write
(
    Ostream& os
) const
{
    mixedFvPatchScalarField::write(os);
    os.writeKeyword("neighbourFieldName")<< neighbourFieldName_
        << token::END_STATEMENT << nl;
    os.writeKeyword("neighbourFieldRadiativeName")<<
        neighbourFieldRadiativeName_ << token::END_STATEMENT << nl;
    temperatureCoupledBase::write(os);
    radiationCoupledBase::write(os);
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

makePatchTypeField
(
    fvPatchScalarField,
    turbulentTemperatureRadiationQinCoupledMixedFvPatchScalarField
);


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace compressible
} // End namespace Foam


// ************************************************************************* //
