// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: packagemanager.cc,v 1.3 1998/07/12 01:26:02 jgg Exp $
/* ######################################################################

   Package Manager - Abstacts the package manager

   More work is needed in the area of transitioning provides, ie exim
   replacing smail. This can cause interesing side effects.

   Other cases involving conflicts+replaces should be tested. 
   
   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#ifdef __GNUG__
#pragma implementation "pkglib/packagemanager.h"
#endif
#include <pkglib/packagemanager.h>
#include <pkglib/orderlist.h>
#include <pkglib/depcache.h>
#include <pkglib/error.h>
#include <pkglib/version.h>
									/*}}}*/

// PM::PackageManager - Constructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgPackageManager::pkgPackageManager(pkgDepCache &Cache) : Cache(Cache)
{
   FileNames = new string[Cache.Head().PackageCount];
   List = 0;
}
									/*}}}*/
// PM::PackageManager - Destructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgPackageManager::~pkgPackageManager()
{
   delete List;
   delete [] FileNames;
}
									/*}}}*/
// PM::FixMissing - Keep all missing packages				/*{{{*/
// ---------------------------------------------------------------------
/* This is called to correct the installation when packages could not
   be downloaded. */
bool pkgPackageManager::FixMissing()
{
   unsigned char *Touch = new unsigned char[Cache.Head().PackageCount];
   for (PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
   {
      // Create the status list that ResolveConflicts needs
      if ((Cache[I].DepState & pkgDepCache::DepNowMin) == pkgDepCache::DepNowMin)
	 Touch[I->ID] = (1 << 0) | (1 << 1);
      else
	 Touch[I->ID] = 1 << 1;
      
      if (Cache[I].Keep() == true)
	 continue;
      if (FileNames[I->ID].empty() == false || Cache[I].Delete() == true)
	 continue;
      Cache.MarkKeep(I);
   }
   
   // Now downgrade everything that is broken
//   Cache.ResolveConflicts(Touch);
   delete [] Touch;

   return Cache.BrokenCount() == 0;
}
									/*}}}*/

// PM::DepAlwaysTrue - Returns true if this dep is irrelevent		/*{{{*/
// ---------------------------------------------------------------------
/* The restriction on provides is to eliminate the case when provides
   are transitioning between valid states [ie exim to smail] */
bool pkgPackageManager::DepAlwaysTrue(DepIterator D)
{
   if (D.TargetPkg()->ProvidesList != 0)
      return false;
   
   if ((Cache[D] & pkgDepCache::DepInstall) != 0 &&
       (Cache[D] & pkgDepCache::DepNow) != 0)
      return true;
   return false;
}
									/*}}}*/
// PM::CheckRConflicts - Look for reverse conflicts			/*{{{*/
// ---------------------------------------------------------------------
/* This looks over the reverses for a conflicts line that needs early
   removal. */
bool pkgPackageManager::CheckRConflicts(PkgIterator Pkg,DepIterator D,
					const char *Ver)
{
   for (;D.end() == false; D++)
   {
      if (D->Type != pkgCache::Dep::Conflicts)
	 continue;
      
      if (D.ParentPkg() == Pkg)
	 continue;
      
      if (pkgCheckDep(D.TargetVer(),Ver,D->CompareOp) == false)
	 continue;

      if (List->IsNow(Pkg) == false)
	 continue;
      
      if (EarlyRemove(D.ParentPkg()) == false)
	 return false;
   }  
   return true;
}
									/*}}}*/
// PM::ConfigureAll - Run the all out configuration			/*{{{*/
// ---------------------------------------------------------------------
/* This configures every package. It is assumed they are all unpacked and
   that the final configuration is valid. */
bool pkgPackageManager::ConfigureAll()
{
   pkgOrderList OList(Cache);
   
   // Populate the order list
   for (pkgOrderList::iterator I = List->begin(); I != List->end(); I++)
      if (List->IsFlag(pkgCache::PkgIterator(Cache,*I),
		       pkgOrderList::UnPacked) == true)
	 OList.push_back(*I);
   
   if (OList.OrderConfigure() == false)
      return false;
   
   // Perform the configuring
   for (pkgOrderList::iterator I = OList.begin(); I != OList.end(); I++)
   {
      PkgIterator Pkg(Cache,*I);
      
      if (Configure(Pkg) == false)
	 return false;
      
      List->Flag(Pkg,pkgOrderList::Configured,pkgOrderList::States);
   }
   
   return true;
}
									/*}}}*/
// PM::SmartConfigure - Perform immediate configuration of the pkg	/*{{{*/
// ---------------------------------------------------------------------
/* This routine scheduals the configuration of the given package and all
   of it's dependents. */
bool pkgPackageManager::SmartConfigure(PkgIterator Pkg)
{
   pkgOrderList OList(Cache);

   if (DepAdd(OList,Pkg) == false)
      return false;
   
   if (OList.OrderConfigure() == false)
      return false;

   // Perform the configuring
   for (pkgOrderList::iterator I = OList.begin(); I != OList.end(); I++)
   {
      PkgIterator Pkg(Cache,*I);
      
      if (Configure(Pkg) == false)
	 return false;
      
      List->Flag(Pkg,pkgOrderList::Configured,pkgOrderList::States);
   }

   // Sanity Check
   if (List->IsFlag(Pkg,pkgOrderList::Configured) == false)
      return _error->Error("Internal error, could not immediate configure %s",Pkg.Name());
   
   return true;
}
									/*}}}*/
// PM::DepAdd - Add all dependents to the oder list			/*{{{*/
// ---------------------------------------------------------------------
/* This recursively adds all dependents to the order list */
bool pkgPackageManager::DepAdd(pkgOrderList &OList,PkgIterator Pkg,int Depth)
{
   if (OList.IsFlag(Pkg,pkgOrderList::Added) == true)
      return true;
   if (List->IsFlag(Pkg,pkgOrderList::Configured) == true)
      return true;
   if (List->IsFlag(Pkg,pkgOrderList::UnPacked) == false)
      return false;
   
   
   // Put the package on the list
   OList.push_back(Pkg);
   OList.Flag(Pkg,pkgOrderList::Added);
   Depth++;

   // Check the dependencies to see if they are all satisfied.
   bool Bad = false;
   for (DepIterator D = Cache[Pkg].InstVerIter(Cache).DependsList(); D.end() == false;)
   {
      if (D->Type != pkgCache::Dep::Depends && D->Type != pkgCache::Dep::PreDepends)
      {
	 D++;
	 continue;
      }
      
      // Grok or groups
      Bad = true;
      for (bool LastOR = true; D.end() == false && LastOR == true; D++)
      {
	 LastOR = (D->CompareOp & pkgCache::Dep::Or) == pkgCache::Dep::Or;
	 
	 if (Bad == false)
	    continue;

	 Version **VList = D.AllTargets();
	 for (Version **I = VList; *I != 0 && Bad == true; I++)
	 {
	    VerIterator Ver(Cache,*I);
	    PkgIterator Pkg = Ver.ParentPkg();

	    // See if the current version is ok
	    if (Pkg.CurrentVer() == Ver && List->IsNow(Pkg) == true && 
		Pkg.State() == PkgIterator::NeedsNothing)
	    {
	       Bad = false;
	       continue;
	    }
	    
	    // Not the install version 
	    if (Cache[Pkg].InstallVer != *I || 
		(Cache[Pkg].Keep() == true && Pkg.State() == PkgIterator::NeedsNothing))
	       continue;
	    if (List->IsFlag(Pkg,pkgOrderList::UnPacked) == true)
	       Bad = !DepAdd(OList,Pkg,Depth);
	    if (List->IsFlag(Pkg,pkgOrderList::Configured) == true)
	       Bad = false;
	 }
	 delete [] VList;
      }
      
      if (Bad == true)
      {
	 OList.Flag(Pkg,0,pkgOrderList::Added);
	 OList.pop_back();
	 Depth--;
	 return false;
      }
   }
   
   Depth--;
   return true;
}
									/*}}}*/
// PM::EarlyRemove - Perform removal of packages before their time	/*{{{*/
// ---------------------------------------------------------------------
/* This is called to deal with conflicts arising from unpacking */
bool pkgPackageManager::EarlyRemove(PkgIterator Pkg)
{
   if (List->IsNow(Pkg) == false)
      return true;
	 
   // Already removed it
   if (List->IsFlag(Pkg,pkgOrderList::Removed) == true)
      return true;
   
   // Woops, it will not be re-installed!
   if (List->IsFlag(Pkg,pkgOrderList::InList) == false)
      return false;
   
   bool Res = SmartRemove(Pkg);
   if (Cache[Pkg].Delete() == false)
      List->Flag(Pkg,pkgOrderList::Removed,pkgOrderList::States);
   
   return Res;
}
									/*}}}*/
// PM::SmartRemove - Removal Helper					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool pkgPackageManager::SmartRemove(PkgIterator Pkg)
{
   if (List->IsNow(Pkg) == false)
      return true;

   List->Flag(Pkg,pkgOrderList::Configured,pkgOrderList::States);
   return Remove(Pkg);
}
									/*}}}*/
// PM::SmartUnPack - Install helper					/*{{{*/
// ---------------------------------------------------------------------
/* This performs the task of handling pre-depends. */
bool pkgPackageManager::SmartUnPack(PkgIterator Pkg)
{
   // Check if it is already unpacked
   if (Pkg.State() == pkgCache::PkgIterator::NeedsConfigure &&
       Cache[Pkg].Keep() == true)
   {
      List->Flag(Pkg,pkgOrderList::UnPacked,pkgOrderList::States);
      if (List->IsFlag(Pkg,pkgOrderList::Immediate) == true)
	 if (SmartConfigure(Pkg) == false)
	    return _error->Error("Internal Error, Could not perform immediate configuraton");
      return true;
   }
   
   /* See if this packages install version has any predependencies
      that are not met by 'now' packages. */
   for (DepIterator D = Cache[Pkg].InstVerIter(Cache).DependsList(); 
	D.end() == false; D++)
   {
      if (D->Type == pkgCache::Dep::PreDepends)
      {
	 // Look for possible ok targets.
	 Version **VList = D.AllTargets();
	 bool Bad = true;
	 for (Version **I = VList; *I != 0 && Bad == true; I++)
	 {
	    VerIterator Ver(Cache,*I);
	    PkgIterator Pkg = Ver.ParentPkg();
	    
	    // See if the current version is ok
	    if (Pkg.CurrentVer() == Ver && List->IsNow(Pkg) == true && 
		Pkg.State() == PkgIterator::NeedsNothing)
	    {
	       Bad = false;
	       continue;
	    }
	 }
	 
	 // Look for something that could be configured.
	 for (Version **I = VList; *I != 0 && Bad == true; I++)
	 {
	    VerIterator Ver(Cache,*I);
	    PkgIterator Pkg = Ver.ParentPkg();
	    
	    // Not the install version 
	    if (Cache[Pkg].InstallVer != *I || 
		(Cache[Pkg].Keep() == true && Pkg.State() == PkgIterator::NeedsNothing))
	       continue;

	    Bad = !SmartConfigure(Pkg);
	 }
	 
	 delete [] VList;
	 
	 if (Bad == true)
	    return _error->Error("Internal Error, Couldn't configure a pre-depend");

	 continue;
      }
      
      if (D->Type == pkgCache::Dep::Conflicts)
      {
	 /* Look for conflicts. Two packages that are both in the install
	    state cannot conflict so we don't check.. */
	 Version **VList = D.AllTargets();
	 for (Version **I = VList; *I != 0; I++)
	 {
	    VerIterator Ver(Cache,*I);
	    PkgIterator Pkg = Ver.ParentPkg();
	    
	    // See if the current version is conflicting
	    if (Pkg.CurrentVer() == Ver && List->IsNow(Pkg) == true)
	    {
	       if (EarlyRemove(Pkg) == false)
		  return _error->Error("Internal Error, Could not early remove %s",Pkg.Name());
	    }
	 }
	 delete [] VList;
      }
   }

   // Check for reverse conflicts.
   CheckRConflicts(Pkg,Pkg.RevDependsList(),
		   Cache[Pkg].InstVerIter(Cache).VerStr());
   for (PrvIterator P = Cache[Pkg].InstVerIter(Cache).ProvidesList(); 
	P.end() == false; P++)
      CheckRConflicts(Pkg,P.ParentPkg().RevDependsList(),P.ProvideVersion());
   
   if (Install(Pkg,FileNames[Pkg->ID]) == false)
      return false;
   
   List->Flag(Pkg,pkgOrderList::UnPacked,pkgOrderList::States);
   
   // Perform immedate configuration of the package.
   if (List->IsFlag(Pkg,pkgOrderList::Immediate) == true)
      if (SmartConfigure(Pkg) == false)
	 return _error->Error("Internal Error, Could not perform immediate configuraton");
   
   return true;
}
									/*}}}*/
// PM::OrderInstall - Installation ordering routine			/*{{{*/
// ---------------------------------------------------------------------
/* */
bool pkgPackageManager::OrderInstall()
{
   delete List;
   List = new pkgOrderList(Cache);
   
   // Generate the list of affected packages and sort it
   for (PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
   {
      // Consider all depends
      if ((I->Flags & pkgCache::Flag::Essential) == pkgCache::Flag::Essential)
      {
	 List->Flag(I,pkgOrderList::Immediate);
	 if (Cache[I].InstallVer != 0)
	    for (DepIterator D = Cache[I].InstVerIter(Cache).DependsList(); 
		 D.end() == false; D++)
	       if (D->Type == pkgCache::Dep::Depends || D->Type == pkgCache::Dep::PreDepends)
		  List->Flag(D.TargetPkg(),pkgOrderList::Immediate);
	 if (I->CurrentVer != 0)
	    for (DepIterator D = I.CurrentVer().DependsList(); 
		 D.end() == false; D++)
	       if (D->Type == pkgCache::Dep::Depends || D->Type == pkgCache::Dep::PreDepends)
		  List->Flag(D.TargetPkg(),pkgOrderList::Immediate);
      }
      
      // Not interesting
      if ((Cache[I].Keep() == true || 
	  Cache[I].InstVerIter(Cache) == I.CurrentVer()) && 
	  I.State() == pkgCache::PkgIterator::NeedsNothing)
	 continue;
      
      // Append it to the list
      List->push_back(I);
      
      if ((I->Flags & pkgCache::Flag::ImmediateConf) == pkgCache::Flag::ImmediateConf)
	 List->Flag(I,pkgOrderList::Immediate);
   }
   
   if (List->OrderUnpack() == false)
      return _error->Error("Internal ordering error");

   for (pkgOrderList::iterator I = List->begin(); I != List->end(); I++)
   {
      PkgIterator Pkg(Cache,*I);
      
      // Sanity check
      if (Cache[Pkg].Keep() == true && Pkg.State() == pkgCache::PkgIterator::NeedsNothing)
	 return _error->Error("Internal Error, trying to manipulate a kept package");
      
      // Perform a delete or an install
      if (Cache[Pkg].Delete() == true)
      {
	 if (SmartRemove(Pkg) == false)
	    return false;	 
      }
      else
	 if (SmartUnPack(Pkg) == false)
	    return false;
   }
   
   // Final run through the configure phase
   if (ConfigureAll() == false)
      return false;

   // Sanity check
   for (pkgOrderList::iterator I = List->begin(); I != List->end(); I++)
      if (List->IsFlag(*I,pkgOrderList::Configured) == false)
	 return _error->Error("Internal error, packages left unconfigured. %s",
			      PkgIterator(Cache,*I).Name());

   return true;
}
									/*}}}*/
// PM::DoInstall - Does the installation				/*{{{*/
// ---------------------------------------------------------------------
/* This uses the filenames in FileNames and the information in the
   DepCache to perform the installation of packages.*/
bool pkgPackageManager::DoInstall()
{
   return OrderInstall() && Go();
}
									/*}}}*/
