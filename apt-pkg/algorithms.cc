// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: algorithms.cc,v 1.2 1998/07/12 01:25:59 jgg Exp $
/* ######################################################################

   Algorithms - A set of misc algorithms

   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#ifdef __GNUG__
#pragma implementation "pkglib/algorithms.h"
#endif 
#include <pkglib/algorithms.h>
#include <pkglib/error.h>
#include <iostream.h>
									/*}}}*/

pkgProblemResolver *pkgProblemResolver::This = 0;

// Simulate::Simulate - Constructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgSimulate::pkgSimulate(pkgDepCache &Cache) : pkgPackageManager(Cache), 
                            Sim(Cache)
{
   Flags = new unsigned char[Cache.HeaderP->PackageCount];
   memset(Flags,0,sizeof(*Flags)*Cache.HeaderP->PackageCount);
}
									/*}}}*/
// Simulate::Install - Simulate unpacking of a package			/*{{{*/
// ---------------------------------------------------------------------
/* */
bool pkgSimulate::Install(PkgIterator iPkg,string /*File*/)
{
   // Adapt the iterator
   PkgIterator Pkg = Sim.FindPkg(iPkg.Name());
   Flags[Pkg->ID] = 1;
   
   cout << "Inst " << Pkg.Name();
   Sim.MarkInstall(Pkg,false);
   
   // Look for broken conflicts+predepends.
   for (PkgIterator I = Sim.PkgBegin(); I.end() == false; I++)
   {
      if (Sim[I].InstallVer == 0)
	 continue;
      
      for (DepIterator D = Sim[I].InstVerIter(Sim).DependsList(); D.end() == false; D++)
	 if (D->Type == pkgCache::Dep::Conflicts || D->Type == pkgCache::Dep::PreDepends)
         {
	    if ((Sim[D] & pkgDepCache::DepInstall) == 0)
	    {
	       cout << " [" << I.Name() << " on " << D.TargetPkg().Name() << ']';
	       if (D->Type == pkgCache::Dep::Conflicts)
		  _error->Error("Fatal, conflicts violated %s",I.Name());
	    }	    
	 }      
   }

   if (Sim.BrokenCount() != 0)
      ShortBreaks();
   else
      cout << endl;
   return true;
}
									/*}}}*/
// Simulate::Configure - Simulate configuration of a Package		/*{{{*/
// ---------------------------------------------------------------------
/* This is not an acurate simulation of relatity, we should really not
   install the package.. For some investigations it may be necessary 
   however. */
bool pkgSimulate::Configure(PkgIterator iPkg)
{
   // Adapt the iterator
   PkgIterator Pkg = Sim.FindPkg(iPkg.Name());
   
   Flags[Pkg->ID] = 2;
//   Sim.MarkInstall(Pkg,false);
   if (Sim[Pkg].InstBroken() == true)
   {
      cout << "Conf " << Pkg.Name() << " broken" << endl;

      Sim.Update();
      
      // Print out each package and the failed dependencies
      for (pkgCache::DepIterator D = Sim[Pkg].InstVerIter(Sim).DependsList(); D.end() == false; D++)
      {
	 if (Sim.IsImportantDep(D) == false || 
	     (Sim[D] & pkgDepCache::DepInstall) != 0)
	    continue;
	 
	 if (D->Type == pkgCache::Dep::Conflicts)
	    cout << " Conflicts:" << D.TargetPkg().Name();
	 else
	    cout << " Depends:" << D.TargetPkg().Name();
      }	    
      cout << endl;

      _error->Error("Conf Broken %s",Pkg.Name());
   }   
   else
      cout << "Conf " <<  Pkg.Name();

   if (Sim.BrokenCount() != 0)
      ShortBreaks();
   else
      cout << endl;
   
   return true;
}
									/*}}}*/
// Simulate::Remove - Simulate the removal of a package			/*{{{*/
// ---------------------------------------------------------------------
/* */
bool pkgSimulate::Remove(PkgIterator iPkg)
{
   // Adapt the iterator
   PkgIterator Pkg = Sim.FindPkg(iPkg.Name());

   Flags[Pkg->ID] = 3;
   Sim.MarkDelete(Pkg);
   cout << "Remv " << Pkg.Name();

   if (Sim.BrokenCount() != 0)
      ShortBreaks();
   else
      cout << endl;

   return true;
}
									/*}}}*/
// Simulate::ShortBreaks - Print out a short line describing all breaks	/*{{{*/
// ---------------------------------------------------------------------
/* */
void pkgSimulate::ShortBreaks()
{
   cout << " [";
   for (PkgIterator I = Sim.PkgBegin(); I.end() == false; I++)
   {
      if (Sim[I].InstBroken() == true)
      {
	 if (Flags[I->ID] == 0)
	    cout << I.Name() << ' ';
/*	 else
	    cout << I.Name() << "! ";*/
      }      
   }
   cout << ']' << endl;
}
									/*}}}*/
// ApplyStatus - Adjust for non-ok packages				/*{{{*/
// ---------------------------------------------------------------------
/* We attempt to change the state of the all packages that have failed
   installation toward their real state. The ordering code will perform 
   the necessary calculations to deal with the problems. */
bool pkgApplyStatus(pkgDepCache &Cache)
{
   for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
   {
      switch (I->CurrentState)
      {
	 // This means installation failed somehow
	 case pkgCache::State::UnPacked:
	 case pkgCache::State::HalfConfigured:
	 Cache.MarkKeep(I);
	 break;

	 // This means removal failed
	 case pkgCache::State::HalfInstalled:
	 Cache.MarkDelete(I);
	 break;
	 
	 default:
	 if (I->InstState != pkgCache::State::Ok)
	    return _error->Error("The package %s is not ok and I "
				 "don't know how to fix it!",I.Name());
      }
   }
   return true;
}
									/*}}}*/
// FixBroken - Fix broken packages					/*{{{*/
// ---------------------------------------------------------------------
/* This autoinstalls every broken package and then runs ScoredFix on the 
   result. */
bool pkgFixBroken(pkgDepCache &Cache)
{
   // Auto upgrade all broken packages
   for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
      if (Cache[I].NowBroken() == true)
	 Cache.MarkInstall(I,true);

   /* Fix packages that are in a NeedArchive state but don't have a
      downloadable install version */
   for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
   {
      if (I.State() != pkgCache::PkgIterator::NeedsUnpack ||
	  Cache[I].Delete() == true)
	 continue;
      
      if (Cache[I].InstVerIter(Cache).Downloadable() == false)
	 continue;

      Cache.MarkInstall(I,true);
   }
   
   pkgProblemResolver Fix(Cache);
   return Fix.Resolve(true);
}
									/*}}}*/
// DistUpgrade - Distribution upgrade					/*{{{*/
// ---------------------------------------------------------------------
/* This autoinstalls every package and then force installs every 
   pre-existing package. This creates the initial set of conditions which 
   most likely contain problems because too many things were installed.
   
   ScoredFix is used to resolve the problems.
 */
bool pkgDistUpgrade(pkgDepCache &Cache)
{
   /* Auto upgrade all installed packages, this provides the basis 
      for the installation */
   for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
      if (I->CurrentVer != 0)
	 Cache.MarkInstall(I,true);

   /* Now, auto upgrade all essential packages - this ensures that
      the essential packages are present and working */
   for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
      if ((I->Flags & pkgCache::Flag::Essential) == pkgCache::Flag::Essential)
	 Cache.MarkInstall(I,true);
   
   /* We do it again over all previously installed packages to force 
      conflict resolution on them all. */
   for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
      if (I->CurrentVer != 0)
	 Cache.MarkInstall(I,false);

   pkgProblemResolver Fix(Cache);
   
   // Hold back held packages.
   for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
   {
      if (I->SelectedState == pkgCache::State::Hold)
      {
	 Fix.Protect(I);
	 Cache.MarkKeep(I);
      }
   }
   
   return Fix.Resolve();
}
									/*}}}*/

// ProblemResolver::pkgProblemResolver - Constructor			/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgProblemResolver::pkgProblemResolver(pkgDepCache &Cache) : Cache(Cache)
{
   // Allocate memory
   unsigned long Size = Cache.HeaderP->PackageCount;
   Scores = new signed short[Size];
   Flags = new unsigned char[Size];
   memset(Flags,0,sizeof(*Flags)*Size);
   
   // Set debug to true to see its decision logic
   Debug = false;
}
									/*}}}*/
// ProblemResolver::ScoreSort - Sort the list by score			/*{{{*/
// ---------------------------------------------------------------------
/* */
int pkgProblemResolver::ScoreSort(const void *a,const void *b)
{
   Package const **A = (Package const **)a;
   Package const **B = (Package const **)b;
   if (This->Scores[(*A)->ID] > This->Scores[(*B)->ID])
      return -1;
   if (This->Scores[(*A)->ID] < This->Scores[(*B)->ID])
      return 1;
   return 0;
}
									/*}}}*/
// ProblemResolver::MakeScores - Make the score table			/*{{{*/
// ---------------------------------------------------------------------
/* */
void pkgProblemResolver::MakeScores()
{
   unsigned long Size = Cache.HeaderP->PackageCount;
   memset(Scores,0,sizeof(*Scores)*Size);

   // Generate the base scores for a package based on its properties
   for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
   {
      if (Cache[I].InstallVer == 0)
	 continue;
      
      signed short &Score = Scores[I->ID];
      
      /* This is arbitary, it should be high enough to elevate an
         essantial package above most other packages but low enough
	 to allow an obsolete essential packages to be removed by
	 a conflicts on a powerfull normal package (ie libc6) */
      if ((I->Flags & pkgCache::Flag::Essential) == pkgCache::Flag::Essential)
	 Score += 100;

      // We transform the priority
      // Important Required Standard Optional Extra
      signed short PrioMap[] = {0,3,2,1,-1,-2};
      if (Cache[I].InstVerIter(Cache)->Priority <= 5)
	 Score += PrioMap[Cache[I].InstVerIter(Cache)->Priority];
      
      /* This helps to fix oddball problems with conflicting packages
         on the same level. We enhance the score of installed packages */
      if (I->CurrentVer != 0)
	 Score += 1;
   }

   // Now that we have the base scores we go and propogate dependencies
   for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
   {
      if (Cache[I].InstallVer == 0)
	 continue;
      
      for (pkgCache::DepIterator D = Cache[I].InstVerIter(Cache).DependsList(); D.end() == false; D++)
      {
	 if (D->Type == pkgCache::Dep::Depends || D->Type == pkgCache::Dep::PreDepends)
	    Scores[D.TargetPkg()->ID]++;
      }
   }   
   
   // Copy the scores to advoid additive looping
   signed short *OldScores = new signed short[Size];
   memcpy(OldScores,Scores,sizeof(*Scores)*Size);
      
   /* Now we cause 1 level of dependency inheritance, that is we add the 
      score of the packages that depend on the target Package. This 
      fortifies high scoring packages */
   for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
   {
      if (Cache[I].InstallVer == 0)
	 continue;
      
      for (pkgCache::DepIterator D = I.RevDependsList(); D.end() == false; D++)
      {
	 // Only do it for the install version
	 if ((pkgCache::Version *)D.ParentVer() != Cache[D.ParentPkg()].InstallVer ||
	     (D->Type != pkgCache::Dep::Depends && D->Type != pkgCache::Dep::PreDepends))
	    continue;	 
	 
	 Scores[I->ID] += abs(OldScores[D.ParentPkg()->ID]);
      }      
   }

   /* Now we propogate along provides. This makes the packages that 
      provide important packages extremely important */
   for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
   {
      for (pkgCache::PrvIterator P = I.ProvidesList(); P.end() == false; P++)
      {
	 // Only do it once per package
	 if ((pkgCache::Version *)P.OwnerVer() != Cache[P.OwnerPkg()].InstallVer)
	    continue;
	 Scores[P.OwnerPkg()->ID] += abs(Scores[I->ID] - OldScores[I->ID]);
      }
   }

   /* Protected things are pushed really high up. This number should put them
      ahead of everything */
   for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
      if ((Flags[I->ID] & Protected) != 0)
	 Scores[I->ID] += 10000;
   
   delete [] OldScores;
}
									/*}}}*/
// ProblemResolver::DoUpgrade - Attempt to upgrade this package		/*{{{*/
// ---------------------------------------------------------------------
/* This goes through and tries to reinstall packages to make this package
   installable */
bool pkgProblemResolver::DoUpgrade(pkgCache::PkgIterator Pkg)
{
   if ((Flags[Pkg->ID] & Upgradable) == 0 || Cache[Pkg].Upgradable() == false)
      return false;
   Flags[Pkg->ID] &= ~Upgradable;
   
   bool WasKept = Cache[Pkg].Keep();
   Cache.MarkInstall(Pkg,false);

   // Isolate the problem dependency
   bool Fail = false;
   for (pkgCache::DepIterator D = Cache[Pkg].InstVerIter(Cache).DependsList(); D.end() == false;)
   {
      // Compute a single dependency element (glob or)
      pkgCache::DepIterator Start = D;
      pkgCache::DepIterator End = D;
      unsigned char State = 0;
      for (bool LastOR = true; D.end() == false && LastOR == true; D++)
      {
	 State |= Cache[D];
	 LastOR = (D->CompareOp & pkgCache::Dep::Or) == pkgCache::Dep::Or;
	 if (LastOR == true)
	    End = D;
      }
      
      // We only worry about critical deps.
      if (End.IsCritical() != true)
	 continue;
      
      // Dep is ok
      if ((Cache[End] & pkgDepCache::DepGInstall) == pkgDepCache::DepGInstall)
	 continue;
      
      // Hm, the group is broken.. I have no idea how to handle this
      if (Start != End)
      {
	 cout << "Note, a broken or group was found in " << Pkg.Name() << "." << endl;
	 Fail = true;
	 break;
      }
            
      // Upgrade the package if the candidate version will fix the problem.
      if ((Cache[Start] & pkgDepCache::DepCVer) == pkgDepCache::DepCVer)
      {
	 PkgIterator P = Start.SmartTargetPkg();
	 if (DoUpgrade(P) == false)
	 {
	    if (Debug == true)
	       cout << "    Reinst Failed because of " << P.Name() << endl;
	    Fail = true;
	    break;
	 }	 
      }
      else
      {
	 /* We let the algorithm deal with conflicts on its next iteration,
	    it is much smarter than us */
	 if (End->Type == pkgCache::Dep::Conflicts)
	    continue;
	 
	 if (Debug == true)
	    cout << "    Reinst Failed early because of " << Start.TargetPkg().Name() << endl;
	 Fail = true;
	 break;
      }      
   }
   
   // Undo our operations - it might be smart to undo everything this did..
   if (Fail == true)
   {
      if (WasKept == true)
	 Cache.MarkKeep(Pkg);
      else
	 Cache.MarkDelete(Pkg);
      return false;
   }	 
   
   if (Debug == true)
      cout << "  Re-Instated " << Pkg.Name() << endl;
   return true;
}
									/*}}}*/
// ProblemResolver::Resolve - Run the resolution pass			/*{{{*/
// ---------------------------------------------------------------------
/* This routines works by calculating a score for each package. The score
   is derived by considering the package's priority and all reverse 
   dependents giving an integer that reflects the amount of breakage that
   adjusting the package will inflict. 
      
   It goes from highest score to lowest and corrects all of the breaks by 
   keeping or removing the dependant packages. If that fails then it removes
   the package itself and goes on. The routine should be able to intelligently
   go from any broken state to a fixed state. 
 
   The BrokenFix flag enables a mode where the algorithm tries to 
   upgrade packages to advoid problems. */
bool pkgProblemResolver::Resolve(bool BrokenFix)
{
 unsigned long Size = Cache.HeaderP->PackageCount;

   // Record which packages are marked for install
   bool Again = false;
   do
   {
      Again = false;
      for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
      {
	 if (Cache[I].Install() == true)
	    Flags[I->ID] |= PreInstalled;
	 else
	 {
	    if (Cache[I].InstBroken() == true && BrokenFix == true)
	    {
	       Cache.MarkInstall(I,false);
	       if (Cache[I].Install() == true)
		  Again = true;
	    }
	    
	    Flags[I->ID] &= ~PreInstalled;
	 }
	 Flags[I->ID] |= Upgradable;
      }
   }
   while (Again == true);

   if (Debug == true)
      cout << "Starting" << endl;
   
   MakeScores();
   
   /* We have to order the packages so that the broken fixing pass 
      operates from highest score to lowest. This prevents problems when
      high score packages cause the removal of lower score packages that
      would cause the removal of even lower score packages. */
   pkgCache::Package **PList = new pkgCache::Package *[Size];
   pkgCache::Package **PEnd = PList;
   for (pkgCache::PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
      *PEnd++ = I;
   This = this;
   qsort(PList,PEnd - PList,sizeof(*PList),&ScoreSort);
   
/* for (pkgCache::Package **K = PList; K != PEnd; K++)
      if (Scores[(*K)->ID] != 0)
      {
	 pkgCache::PkgIterator Pkg(Cache,*K);
	 cout << Scores[(*K)->ID] << ' ' << Pkg.Name() <<
	    ' ' << (pkgCache::Version *)Pkg.CurrentVer() << ' ' << 
	    Cache[Pkg].InstallVer << ' ' << Cache[Pkg].CandidateVer << endl;
      } */

   if (Debug == true)
      cout << "Starting 2" << endl;
   
   /* Now consider all broken packages. For each broken package we either
      remove the package or fix it's problem. We do this once, it should
      not be possible for a loop to form (that is a < b < c and fixing b by
      changing a breaks c) */
   bool Change = true;
   for (int Counter = 0; Counter != 10 && Change == true; Counter++)
   {
      Change = false;
      for (pkgCache::Package **K = PList; K != PEnd; K++)
      {
	 pkgCache::PkgIterator I(Cache,*K);

	 /* We attempt to install this and see if any breaks result,
	    this takes care of some strange cases */
	 if (Cache[I].CandidateVer != Cache[I].InstallVer &&
	     I->CurrentVer != 0 && Cache[I].InstallVer != 0 &&
	     (Flags[I->ID] & PreInstalled) != 0 &&
	     (Flags[I->ID] & Protected) == 0)
	 {
	    if (Debug == true)
	       cout << " Try to Re-Instate " << I.Name() << endl;
	    int OldBreaks = Cache.BrokenCount();
	    pkgCache::Version *OldVer = Cache[I].InstallVer;

	    Cache.MarkInstall(I,false);
	    if (Cache[I].InstBroken() == true || 
		OldBreaks < Cache.BrokenCount())
	    {
	       if (OldVer == 0)
		  Cache.MarkDelete(I);
	       else
		  Cache.MarkKeep(I);
	    }	    
	    else
	       if (Debug == true)
		  cout << "Re-Instated " << I.Name() << endl;
	 }
	    
	 if (Cache[I].InstallVer == 0 || Cache[I].InstBroken() == false)
	    continue;
	 
	 // Isolate the problem dependency
	 PackageKill KillList[100];
	 PackageKill *LEnd = KillList;
	 for (pkgCache::DepIterator D = Cache[I].InstVerIter(Cache).DependsList(); D.end() == false;)
	 {
	    // Compute a single dependency element (glob or)
	    pkgCache::DepIterator Start = D;
	    pkgCache::DepIterator End = D;
	    unsigned char State = 0;
	    for (bool LastOR = true; D.end() == false && LastOR == true; D++)
	    {
	       State |= Cache[D];
	       LastOR = (D->CompareOp & pkgCache::Dep::Or) == pkgCache::Dep::Or;
	       if (LastOR == true)
		  End = D;
	    }
	    
	    // We only worry about critical deps.
	    if (End.IsCritical() != true)
	       continue;
	    
	    // Dep is ok
	    if ((Cache[End] & pkgDepCache::DepGInstall) == pkgDepCache::DepGInstall)
	       continue;
	    
	    // Hm, the group is broken.. I have no idea how to handle this
	    if (Start != End)
	    {
	       cout << "Note, a broken or group was found in " << I.Name() << "." << endl;
	       Cache.MarkDelete(I);
	       break;
	    }
	    	    
	    if (Debug == true)
	       cout << "Package " << I.Name() << " has broken dep on " << End.TargetPkg().Name() << endl;
	    
	    /* Conflicts is simple, decide if we should remove this package
	       or the conflicted one */
	    pkgCache::Version **VList = End.AllTargets();
	    bool Done = false;
	    for (pkgCache::Version **V = VList; *V != 0; V++)
	    {
	       pkgCache::VerIterator Ver(Cache,*V);
	       pkgCache::PkgIterator Pkg = Ver.ParentPkg();
	    
	       if (Debug == true)
		  cout << "  Considering " << Pkg.Name() << ' ' << (int)Scores[Pkg->ID] << 
		  " as a solution to " << I.Name() << ' ' << (int)Scores[I->ID] << endl;
	       if (Scores[I->ID] <= Scores[Pkg->ID] ||
		   ((Cache[End] & pkgDepCache::DepGNow) == 0 &&
		    End->Type != pkgCache::Dep::Conflicts))
	       {
		  if ((Flags[I->ID] & Protected) != 0)
		     continue;
		  
		  // See if a keep will do
		  Cache.MarkKeep(I);
		  if (Cache[I].InstBroken() == false)
		  {
		     if (Debug == true)
			cout << "  Holding Back " << I.Name() << " rather than change " << End.TargetPkg().Name() << endl;
		  }		  
		  else
		  {
		     if (BrokenFix == false || DoUpgrade(I) == false)
		     {
			if (Debug == true)
			   cout << "  Removing " << I.Name() << " rather than change " << End.TargetPkg().Name() << endl;
			Cache.MarkDelete(I);
			if (Counter > 1)
			   Scores[I->ID] = Scores[Pkg->ID];
		     }		     
		  }

		  Change = true;
		  Done = true;
		  break;
	       }
	       else
	       {
		  // Skip this if it is protected
		  if ((Flags[Pkg->ID] & Protected) != 0)
		     continue;
		  
		  LEnd->Pkg = Pkg;
		  LEnd->Dep = End;
		  LEnd++;
		  if (End->Type != pkgCache::Dep::Conflicts)
		     break;
	       }
	    }

	    // Hm, nothing can possibly satisify this dep. Nuke it.
	    if (VList[0] == 0 && End->Type != pkgCache::Dep::Conflicts)
	    {
	       Cache.MarkKeep(I);
	       if (Cache[I].InstBroken() == false)
	       {
		  if (Debug == true)
		     cout << "  Holding Back " << I.Name() << " because I can't find " << End.TargetPkg().Name() << endl;
	       }	       
	       else
	       {
		  if (Debug == true)
		     cout << "  Removing " << I.Name() << " because I can't find " << End.TargetPkg().Name() << endl;
		  Cache.MarkDelete(I);
	       }

	       Change = true;
	       Done = true;
	    }
	    
	    delete [] VList;
	    if (Done == true)
	       break;
	 }
	 
	 // Apply the kill list now
	 if (Cache[I].InstallVer != 0)
	    for (PackageKill *J = KillList; J != LEnd; J++)
         {
	    Change = true;
	    if ((Cache[J->Dep] & pkgDepCache::DepGNow) == 0)
	    {
	       if (J->Dep->Type == pkgCache::Dep::Conflicts)
	       {
		  if (Debug == true)
		     cout << "  Fixing " << I.Name() << " via remove of " << J->Pkg.Name() << endl;
		  Cache.MarkDelete(J->Pkg);
	       }
	    }
	    else
	    {
	       if (Debug == true)
		  cout << "  Fixing " << I.Name() << " via keep of " << J->Pkg.Name() << endl;
	       Cache.MarkKeep(J->Pkg);
	    }
	    
	    if (Counter > 1)
	       Scores[J->Pkg->ID] = Scores[I->ID];
	 }      
      }
   }

   if (Debug == true)
      cout << "Done" << endl;
   
   delete [] Scores;
   delete [] PList;
   
   if (Cache.BrokenCount() != 0)
      return _error->Error("Internal error, ScoredFix generated breaks.");

   return true;
}
									/*}}}*/
