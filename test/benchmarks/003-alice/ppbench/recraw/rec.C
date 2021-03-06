void rec() {

  AliReconstruction reco;

  reco.SetWriteESDfriend();
  reco.SetWriteAlignmentData();

  reco.SetInput("raw.root");

  reco.SetNumberOfEventsPerFile(-1); // all events in one single file

  reco.SetDefaultStorage(Form("local://%s/../OCDB", gSystem->pwd()));
  reco.SetSpecificStorage("GRP/GRP/Data",
			  Form("local://%s/..",gSystem->pwd()));
  reco.SetRunPlaneEff(kTRUE);

  reco.SetRunReconstruction("ALL") ;
  reco.SetRunQA("ALL:ALL") ;
  
  reco.SetQARefDefaultStorage("local://$ALICE_ROOT/QAref") ;
  
  for (Int_t det = 0 ; det < AliQA::kNDET ; det++) {
    reco.SetQACycles((AliQAv1::DETECTORINDEX_t)det, 999) ;
    reco.SetQAWriteExpert((AliQAv1::DETECTORINDEX_t)det) ; 
  }

  TStopwatch timer;
  timer.Start();
  reco.Run();
  timer.Stop();
  timer.Print();
}
