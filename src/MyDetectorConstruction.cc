// =========================================================================
// Header Definition
// =========================================================================

// 1. Include Local headers
#include "MyDetectorConstruction.hh" // Geometry definition class
#include "MySensitiveDetector.hh"    // Custom Sensitive Detector

// 2. Include Geant4 headers
#include "G4NistManager.hh"          // Material database
#include "G4Box.hh"                  // Box geometry
#include "G4Tubs.hh"                 // Cylinder geometry
#include "G4Sphere.hh"
#include "G4LogicalVolume.hh"        // Logical volume
#include "G4VPhysicalVolume.hh"
#include "G4PVPlacement.hh"          // Physical volume placement
#include "G4SubtractionSolid.hh"     // Boolean subtraction solid
#include "G4VisAttributes.hh"        // Visualization attributes
#include "G4Colour.hh"               // Colors
#include "G4Region.hh"               // Regions
#include "G4SDManager.hh"            // Sensitive detector manager
#include "G4SystemOfUnits.hh"        // Units
#include "G4RegionStore.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4IStore.hh"
#include "G4RunManager.hh"           // Run manager
#include "G4OpticalSurface.hh"
#include "G4MaterialPropertiesTable.hh"
#include "G4LogicalSkinSurface.hh"
#include "G4ProductionCuts.hh"

// =========================================================================
// Geometry Dimensions
// =========================================================================
namespace GEO {
	// ===============================================
	// 1. World Volume
	// ===============================================
	const G4double WorldSize = 3 * m;

	// ===============================================
	// 2. Pb Shield (PHITS cell 120) + inner vacuum cavity (cell 110)
	//    Shield: cylinder R = 8.5 cm, z from -5 cm to the NaI back face
	//    Cavity: R = 3.5 cm, z from 0 to the NaI front face
	// ===============================================
	const G4double Shield_Radius = 8.5 * cm;
	const G4double Shield_ZMin = -5 * cm;
	const G4double Cavity_Radius = 3.5 * cm;

	// ===============================================
	// 3. Source Holder (PHITS cell 107): Pb ring, z = 0 -> 1 cm
	//    The well inside (R = 1 cm) is vacuum; the beam starts at its bottom
	// ===============================================
	const G4double SourceHolder_InnerRadius = 1 * cm;
	const G4double SourceHolder_Height = 1 * cm;
	// Source disk (PHITS cell 108, vacuum): symbolic only, z = 0.5 -> 1 cm
	const G4double Source_Radius = 1 * cm;
	const G4double Source_Thickness = 5 * mm;

	// ===============================================
	// 4. First Collimator (PHITS cell 101): z = 1 -> 7 cm
	// ===============================================
	const G4double Collimator1_InnerRadius = 4 * mm;
	const G4double Collimator1_OutterRadius = 35 * mm;
	const G4double Collimator1_Height = 6 * cm;


	// ===============================================
	// 5. Sample (PHITS cell 103): z = 7 cm -> 7 cm + t
	// ===============================================
	const G4double Sample_Radius = 1 * cm;
	const G4double Sample_Thickness = 5 * mm;

	// ===============================================
	// 6. 2nd Collimator (PHITS cell 104): 3 cm long, right after the sample
	// ===============================================
	const G4double Collimator2_InnerRadius = 4 * mm;
	const G4double Collimator2_OutterRadius = 25 * mm;
	const G4double Collimator2_Height = 15 * cm;

	// ===============================================
	// 7. NaI Detector (PHITS cell 106), embedded in the Pb shield
	// ===============================================
	const G4double Solid_NaI_Radius = 2.5 * cm;
	const G4double Solid_NaI_Height = 5 * cm;

	// ===============================================


}

// =========================================================================
// Constructor / Destructor
// =========================================================================
MyDetectorConstruction::MyDetectorConstruction()
	: G4VUserDetectorConstruction(),
	fMessenger(nullptr),
	fSample_Thickness(5.0 * mm), // Default HDPE+hBN Layer Thickness
	fSampleMaterial("S1")        // NEW: default sample material, can be changed via messenger
{
	// Messenger for setting the volume fraction of hBN in the composite material
	fMessenger = new G4GenericMessenger(this, "/DetectorConstruction/", "Detector Construction Control");
	auto& fSample_Thicknesscmd = fMessenger->DeclareProperty("Sample_Thickness", fSample_Thickness, "Set Sample_Thickness");
	// NEW: allow choosing which sample material (S1, S2, S3, S4, S7, ...) to use for the run
	auto& fSampleMaterialCmd = fMessenger->DeclareProperty("Sample_Material", fSampleMaterial, "Set Sample material name (e.g. S1, S2, S3...)");
	fMessenger->DeclareMethod("update", &MyDetectorConstruction::UpdateGeometry, "Update geometry after parameter changes");

}
MyDetectorConstruction::~MyDetectorConstruction() {}

// =========================================================================
// Define Materials
// =========================================================================
void MyDetectorConstruction::DefineMaterials() {
	G4NistManager* NIST = G4NistManager::Instance();
	// ===============================================
	// 1. NIST Materials
	// ===============================================
	NIST->FindOrBuildMaterial("G4_AIR");
	NIST->FindOrBuildMaterial("G4_Galactic");
	NIST->FindOrBuildMaterial("G4_SODIUM_IODIDE");
	NIST->FindOrBuildMaterial("G4_Pb");


	// ===============================================
	// 2. Sample Materials
	// ===============================================

	// [0]. S0: "No sample" placeholder - vacuum, so the Sample volume
	//      still exists geometrically (Collimator2 position unaffected)
	//      but is effectively transparent to radiation.
	//      Select via macro:
	//        /DetectorConstruction/Sample_Material S0
	//        /DetectorConstruction/update
	if (!G4Material::GetMaterial("S0", false)) {
		new G4Material("S0", 1, 1.008 * g / mole, 1e-25 * g / cm3,
			kStateGas, 2.73 * kelvin, 3e-18 * pascal);
	}

	// Glass system composition based on B-O-Mg-Zn-Ba-Ce mass fractions
	// (ZAID mapping: 5000=B, 8000=O, 12000=Mg, 30000=Zn, 56000=Ba, 58000=Ce)
	// [1]. S1
	if (!G4Material::GetMaterial("S1", false)) {
		G4Material* S1 = new G4Material("S1", 2.2816 * g / cm3, 5);
		S1->AddElement(NIST->FindOrBuildElement("C"), 0.2258);
		S1->AddElement(NIST->FindOrBuildElement("O"), 0.6347);
		S1->AddElement(NIST->FindOrBuildElement("Na"), 0.0615);
		S1->AddElement(NIST->FindOrBuildElement("Mg"), 0.0186);
		S1->AddElement(NIST->FindOrBuildElement("Si"), 0.0594);
	}

	// [2]. S2
	if (!G4Material::GetMaterial("S2", false)) {
		G4Material* S2 = new G4Material("S2", 2.3205 * g / cm3, 5);
		S2->AddElement(NIST->FindOrBuildElement("C"), 0.1411);
		S2->AddElement(NIST->FindOrBuildElement("O"), 0.6687);
		S2->AddElement(NIST->FindOrBuildElement("Na"), 0.0643);
		S2->AddElement(NIST->FindOrBuildElement("Mg"), 0.0277);
		S2->AddElement(NIST->FindOrBuildElement("Si"), 0.0983);
	}

	// [3]. S3
	if (!G4Material::GetMaterial("S3", false)) {
		G4Material* S3 = new G4Material("S3", 2.3159 * g / cm3, 5);
		S3->AddElement(NIST->FindOrBuildElement("C"), 0.1476);
		S3->AddElement(NIST->FindOrBuildElement("O"), 0.6712);
		S3->AddElement(NIST->FindOrBuildElement("Na"), 0.0680);
		S3->AddElement(NIST->FindOrBuildElement("Mg"), 0.0243);
		S3->AddElement(NIST->FindOrBuildElement("Si"), 0.0890);
	}

	// [4]. S4
	if (!G4Material::GetMaterial("S4", false)) {
		G4Material* S4 = new G4Material("S4", 2.3881 * g / cm3, 5);
		S4->AddElement(NIST->FindOrBuildElement("C"), 0.1671);
		S4->AddElement(NIST->FindOrBuildElement("O"), 0.6784);
		S4->AddElement(NIST->FindOrBuildElement("Na"), 0.0639);
		S4->AddElement(NIST->FindOrBuildElement("Mg"), 0.0221);
		S4->AddElement(NIST->FindOrBuildElement("Si"), 0.0685);
	}

	// [5]. S7: pure Pb
	if (!G4Material::GetMaterial("S7", false)) {
		G4Material* S7 = new G4Material("S7", 11.34 * g / cm3, 1);
		S7->AddElement(NIST->FindOrBuildElement("Pb"), 1.0);
	}

}

// =========================================================================
// Construct Geometry
// =========================================================================
G4VPhysicalVolume* MyDetectorConstruction::Construct() {
	DefineMaterials();
	G4NistManager* NIST = G4NistManager::Instance();
	G4double FITNESS = 0.1 * mm; // For boolean solids, set to 1.0 for best performance

	// ===============================================
	// 1. World Volume
	// ===============================================
	G4Box* solidWorld = new G4Box("World", GEO::WorldSize / 2, GEO::WorldSize / 2, GEO::WorldSize / 2);
	G4LogicalVolume* World_LV = new G4LogicalVolume(solidWorld, NIST->FindOrBuildMaterial("G4_Galactic"), "WorldLV");
	World_Phys = new G4PVPlacement(nullptr, {}, World_LV, "World", nullptr, false, 0, true);

	// Absolute z positions (same frame as the PHITS input):
	// everything downstream of the sample shifts with the sample thickness t
	G4double Collimator1_ZMin = GEO::SourceHolder_Height;                  // 1 cm
	G4double Sample_ZMin = Collimator1_ZMin + GEO::Collimator1_Height;     // 7 cm
	G4double Collimator2_ZMin = Sample_ZMin + fSample_Thickness;           // 7 cm + t
	G4double NaI_ZMin = Collimator2_ZMin + GEO::Collimator2_Height;        // 10 cm + t
	G4double NaI_ZMax = NaI_ZMin + GEO::Solid_NaI_Height;                  // 15 cm + t

	// ===============================================
	// 2. Pb Shield (mother of the cavity and the NaI crystal)
	// ===============================================
	G4Tubs* solidShield = new G4Tubs("Shield", 0, GEO::Shield_Radius, (NaI_ZMax - GEO::Shield_ZMin) / 2, 0, 360 * deg);
	G4LogicalVolume* Shield_LV = new G4LogicalVolume(solidShield, NIST->FindOrBuildMaterial("G4_Pb"), "Shield_LV");
	G4double Shield_PosZ = (GEO::Shield_ZMin + NaI_ZMax) / 2;
	new G4PVPlacement(nullptr, G4ThreeVector(0, 0, Shield_PosZ), Shield_LV, "Shield", World_LV, false, 0, true);

	// Vacuum cavity inside the shield: holds source holder, collimators and sample
	G4Tubs* solidCavity = new G4Tubs("Cavity", 0, GEO::Cavity_Radius, NaI_ZMin / 2, 0, 360 * deg);
	G4LogicalVolume* Cavity_LV = new G4LogicalVolume(solidCavity, NIST->FindOrBuildMaterial("G4_Galactic"), "Cavity_LV");
	G4double Cavity_PosZ = NaI_ZMin / 2;
	new G4PVPlacement(nullptr, G4ThreeVector(0, 0, Cavity_PosZ - Shield_PosZ), Cavity_LV, "Cavity", Shield_LV, false, 0, true);

	// ===============================================
	// 3. Source Holder (Pb ring around the source well)
	// ===============================================
	G4Tubs* solidSourceHolder = new G4Tubs("SourceHolder", GEO::SourceHolder_InnerRadius, GEO::Cavity_Radius, GEO::SourceHolder_Height / 2, 0, 360 * deg);
	G4LogicalVolume* SourceHolder_LV = new G4LogicalVolume(solidSourceHolder, NIST->FindOrBuildMaterial("G4_Pb"), "SourceHolder_LV");
	G4double SourceHolder_PosZ = GEO::SourceHolder_Height / 2;
	new G4PVPlacement(nullptr, G4ThreeVector(0, 0, SourceHolder_PosZ - Cavity_PosZ), SourceHolder_LV, "SourceHolder", Cavity_LV, false, 0, true);

	// Source disk at the bottom of the well: vacuum, only marks where the source is
	// (the pencil beam starts on its top face, z = 1 cm)
	G4Tubs* solidSource = new G4Tubs("Source", 0, GEO::Source_Radius, GEO::Source_Thickness / 2, 0, 360 * deg);
	G4LogicalVolume* Source_LV = new G4LogicalVolume(solidSource, NIST->FindOrBuildMaterial("G4_Galactic"), "Source_LV");
	G4double Source_PosZ = GEO::SourceHolder_Height - GEO::Source_Thickness / 2;
	new G4PVPlacement(nullptr, G4ThreeVector(0, 0, Source_PosZ - Cavity_PosZ), Source_LV, "Source", Cavity_LV, false, 0, true);

	// ===============================================
	// 4. Collimator 1
	// ===============================================
	G4Tubs* solidCollimator1 = new G4Tubs("Collimator1", GEO::Collimator1_InnerRadius, GEO::Collimator1_OutterRadius, GEO::Collimator1_Height / 2, 0, 360 * deg);
	G4LogicalVolume* Collimator1_LV = new G4LogicalVolume(solidCollimator1, NIST->FindOrBuildMaterial("G4_Pb"), "Collimator1_LV");
	G4double Collimator1_PosZ = Collimator1_ZMin + GEO::Collimator1_Height / 2;
	new G4PVPlacement(nullptr, G4ThreeVector(0, 0, Collimator1_PosZ - Cavity_PosZ), Collimator1_LV, "Collimator1", Cavity_LV, false, 0, true);

	// ===============================================
	// 5. Sample
	// ===============================================
	G4Tubs* solidSample = new G4Tubs("Sample", 0, GEO::Sample_Radius, fSample_Thickness / 2, 0, 360 * deg);
	// NEW: material is no longer hardcoded to "S1", it now comes from fSampleMaterial
	//      (settable at runtime via /DetectorConstruction/Sample_Material <name>)
	G4Material* sampleMat = G4Material::GetMaterial(fSampleMaterial, false);
	if (!sampleMat) {
		G4cerr << "\n*** ERROR: Sample material \"" << fSampleMaterial
			<< "\" not found (did you define it in DefineMaterials()?). "
			<< "Falling back to S1.\n" << G4endl;
		sampleMat = G4Material::GetMaterial("S1");
	}
	G4LogicalVolume* Sample_LV = new G4LogicalVolume(solidSample, sampleMat, "Sample_LV");
	G4double Sample_PosZ = Sample_ZMin + fSample_Thickness / 2;
	new G4PVPlacement(nullptr, G4ThreeVector(0, 0, Sample_PosZ - Cavity_PosZ), Sample_LV, "Sample", Cavity_LV, false, 0, true);

	// ===============================================
	// 6. Collimator 2
	// ===============================================
	G4Tubs* solidCollimator2 = new G4Tubs("Collimator2", GEO::Collimator2_InnerRadius, GEO::Collimator2_OutterRadius, GEO::Collimator2_Height / 2, 0, 360 * deg);
	G4LogicalVolume* Collimator2_LV = new G4LogicalVolume(solidCollimator2, NIST->FindOrBuildMaterial("G4_Pb"), "Collimator2_LV");
	G4double Collimator2_PosZ = Collimator2_ZMin + GEO::Collimator2_Height / 2;
	new G4PVPlacement(nullptr, G4ThreeVector(0, 0, Collimator2_PosZ - Cavity_PosZ), Collimator2_LV, "Collimator2", Cavity_LV, false, 0, true);


	// ===============================================
	// 7. NaI Crystal (sits in the shield, right after the cavity)
	// ===============================================
	G4Tubs* solidNaI = new G4Tubs("NaI", 0, GEO::Solid_NaI_Radius, GEO::Solid_NaI_Height / 2, 0, 360 * deg);
	G4LogicalVolume* NaI_LV = new G4LogicalVolume(solidNaI, NIST->FindOrBuildMaterial("G4_SODIUM_IODIDE"), "NaI_LV");
	G4double NaI_PosZ = NaI_ZMin + GEO::Solid_NaI_Height / 2;
	new G4PVPlacement(nullptr, G4ThreeVector(0, 0, NaI_PosZ - Shield_PosZ), NaI_LV, "NaI", Shield_LV, false, 0, true);


	// ===============================================
	// VISUALIZATION
	// ===============================================

	// 1. World: invisible
	G4VisAttributes* World_VisAtt = new G4VisAttributes(G4Colour(1, 1, 1, 0.0));
	World_VisAtt->SetVisibility(false);
	World_LV->SetVisAttributes(World_VisAtt);

	// 2. Pb Shield: Green, solid (use a cutaway plane to see inside, see init_vis.mac)
	G4VisAttributes* Shield_VisAtt = new G4VisAttributes(G4Colour(0, 1, 0));
	Shield_VisAtt->SetForceSolid(true);
	Shield_LV->SetVisAttributes(Shield_VisAtt);

	// Vacuum cavity: Black, so the gaps and the channels show up in the cut view
	// (like the shield, it hides the inside unless a cutaway plane is set)
	G4VisAttributes* Cavity_VisAtt = new G4VisAttributes(G4Colour(0.05, 0.05, 0.05));
	Cavity_VisAtt->SetForceSolid(true);
	Cavity_LV->SetVisAttributes(Cavity_VisAtt);

	// Source disk: White
	G4VisAttributes* Source_VisAtt = new G4VisAttributes(G4Colour(1, 1, 1));
	Source_VisAtt->SetForceSolid(true);
	Source_LV->SetVisAttributes(Source_VisAtt);

	// 3. Source Holder and Collimators: Magenta
	G4VisAttributes* Collimator_VisAtt = new G4VisAttributes(G4Colour(0.75, 0, 0.5));
	Collimator_VisAtt->SetForceSolid(true);
	SourceHolder_LV->SetVisAttributes(Collimator_VisAtt);
	Collimator1_LV->SetVisAttributes(Collimator_VisAtt);
	Collimator2_LV->SetVisAttributes(Collimator_VisAtt);

	// 4. Sample: Blue
	G4VisAttributes* Sample_VisAtt = new G4VisAttributes(G4Colour(0, 0, 1));
	Sample_VisAtt->SetForceSolid(true);
	Sample_LV->SetVisAttributes(Sample_VisAtt);

	// 5. NaI Detector: Yellow
	G4VisAttributes* NaI_VisAtt = new G4VisAttributes(G4Colour(1, 1, 0));
	NaI_VisAtt->SetForceSolid(true);
	NaI_LV->SetVisAttributes(NaI_VisAtt);

	// ===============================================
	// ADDITIONAL SETTINGS
	// ===============================================

	// 1. Define a region for the Sample to set production cuts
	G4Region* Sample_Region = G4RegionStore::GetInstance()->FindOrCreateRegion("SampleRegion");
	Sample_Region->AddRootLogicalVolume(Sample_LV);
	G4ProductionCuts* sampleCuts = new G4ProductionCuts();
	sampleCuts->SetProductionCut(1 * nm, G4ProductionCuts::GetIndex("gamma"));
	sampleCuts->SetProductionCut(1 * nm, G4ProductionCuts::GetIndex("e-"));
	sampleCuts->SetProductionCut(1 * nm, G4ProductionCuts::GetIndex("e+"));
	Sample_Region->SetProductionCuts(sampleCuts);

	return World_Phys;
}

void MyDetectorConstruction::UpdateGeometry() {
	auto runManager = G4RunManager::GetRunManager();
	runManager->ReinitializeGeometry();         // rebuild geometry
	ConstructSDandField();                      // <--- reattach SDs manually
	// Print updated parameters
	G4cout << "\n================ Updated Geometry ================\n";
	G4cout << " Sample Thickness: " << fSample_Thickness / mm << " mm\n";
	G4cout << " Sample Material : " << fSampleMaterial << "\n";
}

// NEW: getter so other classes (e.g. MyRunAction) can tag their output
// with the material that was actually used for that run.
G4String MyDetectorConstruction::GetSampleMaterialName() const {
	return fSampleMaterial;
}

// NEW: getter so other classes (e.g. MyRunAction) can tag their output
// with the sample thickness actually used for that run.
G4double MyDetectorConstruction::GetSampleThickness() const {
	return fSample_Thickness;
}

// =========================================================================
// Construct Sensitive Detectors
// =========================================================================
void MyDetectorConstruction::ConstructSDandField() {

	G4cout << "\n================ ConstructSDandField() =================\n";

	// --- 1. Lấy SDManager và LogicalVolumeStore ---
	auto sdManager = G4SDManager::GetSDMpointer();
	auto LVS = G4LogicalVolumeStore::GetInstance();

	// --- 2. NaI Detector ---
	G4String SDname = "NaI_SD";
	auto NaI_Detector = dynamic_cast<MySensitiveDetector*>(sdManager->FindSensitiveDetector(SDname, false));
	if (!NaI_Detector) {
		NaI_Detector = new MySensitiveDetector(SDname, "NaIHitsCollection");
		sdManager->AddNewDetector(NaI_Detector);
		//G4cout << "[SD] Created new NaI_Detector" << G4endl;
	}
	else {
		//G4cout << "[SD] Reusing existing NaI_Detector" << G4endl;
	}
	// Gán SD
	for (auto lv : *LVS) {
		if (lv->GetName() == "NaI_LV") {
			lv->SetSensitiveDetector(NaI_Detector);
			//G4cout << "Attached NaI_Detector to " << lv->GetName() << G4endl;
		}
	}

	G4cout << "========================================================\n\n";
}