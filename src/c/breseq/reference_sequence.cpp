/*****************************************************************************
 
 AUTHORS
 
 Jeffrey E. Barrick <jeffrey.e.barrick@gmail.com>
 David B. Knoester
 
 LICENSE AND COPYRIGHT
 
 Copyright (c) 2008-2010 Michigan State University
 Copyright (c) 2011-2017 The University of Texas at Austin
 
 breseq is free software; you can redistribute it and/or modify it under the
 terms the GNU General Public License as published by the Free Software
 Foundation; either version 1, or (at your option) any later version.
 
 *****************************************************************************/

#include "libbreseq/reference_sequence.h"

#include "libbreseq/error_count.h"
#include "libbreseq/genome_diff.h"


using namespace std;

namespace breseq {
  
  const string cSequenceFeature::feature_list_separator = ";";
  
  
  // this could be moved to the object
  const string cReferenceSequences::intergenic_separator = "/";
  const string cReferenceSequences::html_intergenic_separator = "/";
  const string cReferenceSequences::no_gene_name = "–"; //en-dash
  const string cReferenceSequences::multiple_separator = cSequenceFeature::feature_list_separator;
  const string cReferenceSequences::html_multiple_separator = "<br>";
  const double cReferenceSequences::k_inactivate_overlap_fraction = 0.8;
  const int32_t cReferenceSequences::k_promoter_distance = 150;

  
  const vector<string> snp_types = make_vector<string>("synonymous")("nonsynonymous")("nonsense")("noncoding")("pseudogene")("intergenic");
  
  const string BULL_DUMMY_SEQ_ID = "__BULL_DUMMY_SEQ_ID__";
  
  
  
  cFeatureLocation::cFeatureLocation(cSequenceFeature* feature, const cLocation& loc)
    : cLocation(loc)
    , m_feature(feature)
  {
    m_index = feature->m_locations.size()+1;
    
    if (m_index == 1) {
      m_offset = feature->m_locations.back().get_end_1() - feature->m_locations.back().get_start_1();
    } else {
      m_offset = 1;
    }
  }

  
  // This constructor takes into account
  // any previously assigned locations in feature
  // to automatically fill in m_index and m_offset
  cFeatureLocation::cFeatureLocation(
                                     cSequenceFeature* feature,
                                     int32_t start_1,
                                     int32_t end_1,
                                     int8_t strand,
                                     bool start_is_indeterminate,
                                     bool end_is_indeterminate
                                     )
  {
    cFeatureLocation(feature, cLocation(start_1, end_1, strand, start_is_indeterminate, end_is_indeterminate));
  }
  
  string cSequenceFeature::get_nucleotide_sequence(const cAnnotatedSequence& seq) const
  {
    string s;
    for(cFeatureLocationList::const_iterator it = m_locations.begin(); it != m_locations.end(); it++ ) {
      s += seq.get_stranded_sequence_1( it->get_strand(), it->get_start_1(), it->get_end_1() );
    }
    return s;
  }
  
  
  // @JEB: 2016-10-04
  // changed to return an index_1 of 0 and a strand of 0 instead of an error if position does not overlap gene
  void cSequenceFeature::genomic_position_to_index_strand_1(int32_t pos_1, int32_t& index_1, int8_t& strand) const
  {
    index_1 = 0;
    strand = 0;
    for(cFeatureLocationList::const_iterator it = m_locations.begin(); it != m_locations.end(); it++ ) {

      if ( (pos_1 >= it->get_start_1()) && (pos_1 <= it->get_end_1()) ) {
        
        strand = it->get_strand();
        if (strand == +1) {
          index_1 += pos_1 - it->get_start_1() + 1;
        } else {
          index_1 += it->get_end_1() - pos_1 + 1;
        }
        return;
      }
          
      // Add the entire length of this region
      index_1 += it->get_end_1() - it->get_start_1() + 1;
    }
      
    //ERROR("Cannot find index of position that is not within gene.");
  }
  
  string cAnnotatedSequence::get_stranded_sequence_1(int32_t strand, int32_t start_1, int32_t end_1) const
  {
    ASSERT( (strand==-1) || (strand ==+1), "Expected strand +/-1. Provided: " + to_string(strand));
    
    return ((strand==1)
            ? get_sequence_1(start_1, end_1)
            : reverse_complement(get_sequence_1(start_1, end_1))
            );
  }
  
  char cAnnotatedSequence::get_stranded_sequence_1(int32_t strand, int32_t pos_1) const
  {
    ASSERT( (strand==-1) || (strand ==+1), "Expected strand +/-1. Provided: " + to_string(strand));
    
    return ((strand==1)
            ? get_sequence_1(pos_1)
            : reverse_complement(get_sequence_1(pos_1))
            );
  }
  
  string cAnnotatedSequence::get_circular_sequence_1(int32_t start_1, uint32_t size) const
  {
    // This is meant to be robust to
    // (1) Choosing negative start_1 or values 
    //     greater than the sequence length
    // (2) Sizes that are bigger than the genome
    
    //-- First, put the start position in bounds.

    // If start_1 is negative, move forward by genome-sized
    // chunks until it is within bounds
    while (start_1 < 1) {
      start_1 = this->get_sequence_length() + start_1;
    }
    
    //If start_1 is too large, set to where it lines up if you go round and round the genome.
    start_1 = start_1 % this->get_sequence_length();
    if (start_1 == 0) start_1 = this->get_sequence_length();
    //-- After all that, start1 should be in the range [1,sequence_size]
    
    // Build the return sequence, which may wrap around from the end to beginning of genome
    string ret_val = "";
    
    while (size > ret_val.size()) {
      
      uint32_t leftover_genome_size = this->get_sequence_length() - (start_1 - 1);
      int32_t chunk_size = leftover_genome_size < size ? leftover_genome_size : size;
      
      ret_val += this->get_sequence_1(start_1, start_1 + chunk_size - 1);
      size -= chunk_size;
      start_1 = 1;
    }
    
    return ret_val;
  }
  
  // Closest distance, savvy to circular chromosomes
  int32_t cAnnotatedSequence::get_circular_distance_1(int32_t pos_1, int32_t pos_2) const
  {
    int32_t dist = abs(pos_1 - pos_2);
    if (m_is_circular) {
      dist = min(dist, m_length - dist);
    }
    return dist;
  }

  
  // Replace Sequence with Input
  // Every position between and including start_1 and end_1 will be replaced with replacement_seq.
  // This function will shift everything else.
  // mut_type is used to append the type of mutation to the feature notes.
  // verbose outputs messages to console.
  // 
  // Successfully checks all three feature lists.  m_features, m_genes, and m_repeats.
  void cAnnotatedSequence::replace_sequence_1(int32_t start_1, int32_t end_1, const string &replacement_seq, string mut_type, bool verbose)
  {
    ASSERT(start_1 <= end_1, "start (" + to_string(start_1) + ") not less than or equal to end (" + to_string(end_1) + ")");
    m_fasta_sequence.replace_sequence_1(start_1, end_1, replacement_seq);
    
    //Temporary variable for the amount to shift the start and end positions
    // Example:
    //   If we're replacing positions 5 and 6, we need to potentially shift  
    //   position 7 down by 2 even though the difference between 5 and 6 is 1.
    //   This is why we add 1.  We also need to take the new replacement
    //   length into account.
    int32_t shift = (end_1-start_1 + 1) - replacement_seq.length();
    
    //Modify the length of the sequence
    m_length -= shift;
    
    //Notify what mutation is being worked on
    if(verbose)cout << "** " << mut_type << " **" << endl;
    
    // We will be erasing inside the it loop.  This is to keep
    // track of whether or not we should iterate to the next element.
    bool it_feature_iterate = true;
    
    //Iterate through all the features
    for (list<cSequenceFeaturePtr>::iterator it_feature = m_features.begin(); it_feature != m_features.end(); )
    {
      it_feature_iterate = true;
      //The current feature we're looking at
      cSequenceFeature& feat = **it_feature;
      
      // Iterature through all regions of the feature
      bool it_region_iterate = true;
      
      for (cFeatureLocationList::iterator it_region = feat.m_locations.begin(); it_region != feat.m_locations.end(); )
      {
        it_region_iterate = true;
        cFeatureLocation& region = *it_region;

      
        //Does the region start and end inside of the replacement?
        if(region.get_start_1() >= start_1 && region.get_end_1() <= end_1)
        {
          it_region = feat.m_locations.erase(it_region);
          it_region_iterate = false;
        }
        
        //Does the feature end after the replacement starts?
        else if(region.get_end_1() > start_1 && region.get_end_1() < end_1)
        {
          //Temporary variable for the new end position
          uint32_t end_temp = start_1 - 1;
          
          //Notify the user of the action
          if(verbose){cout << "MODIFY\t" << feat["type"]<< "\t" << feat.m_gff_attributes["ID"] << " " << feat.m_gff_attributes["Name"] << endl;}
          
          //Modify the end of the feature
          region.set_end_1(end_temp);
          
          //Mark it as pseudo
          feat.flag_pseudo(verbose);
          
          //Modify the notes for this feature
          //If this feature is already pseudo or a region, do nothing.
          //if(feat["type"] != "region" && feat["type"] != "source" && !feat.m_gff_attributes.count("Mutation from " + mut_type))feat.m_gff_attributes["Note"].push_back("Mutation from " + mut_type);
        }
        
        //Everything that starts after the replacement starts needs to be shifted          
        else if(region.get_start_1() > start_1)
        {
          //Does the feature start before the replacement ends?
          if(region.get_start_1() < end_1)
          {
            //Temporary variable for the new start postion
            uint32_t start_temp = end_1 + 1;
            
            //Notify the user of the action
            if(verbose){cout << "MODIFY\t" << feat["type"] << "\t" << feat.m_gff_attributes["ID"] << " " << feat.m_gff_attributes["Name"] << endl;}
            
            //Modify the start of the feature
            region.set_start_1(start_temp);
            
            //Mark it as pseudo
            feat.flag_pseudo(verbose);
            
            //Modify the notes for this feature
            //if(feat["type"] != "region" && feat["type"] != "source" && !feat.m_gff_attributes.count("Mutation from " + mut_type))feat.m_gff_attributes["Note"].push_back("Mutation from " + mut_type);
          }             
          
          //Is there any reason to shift?
          if(shift)
          {          
            //Modify  both the start and end of the region
            region.set_start_end_1(region.get_start_1() - shift, region.get_end_1() - shift);
            
            //Notify the user of the action
            if(verbose){cout << "SHIFT\t" << feat["type"] << "\t" << feat.m_gff_attributes["ID"] << " " << feat.m_gff_attributes["Name"] << endl;}
          }
          
        }
        
        //Any feature the encompases the replaced sequence needs to be resized
        else if(region.get_start_1() <= start_1 && region.get_end_1() >= end_1)
        {                          
          //Is there anything to modify?
          if(shift)
          {
            //Temporary variable for the new end position
            uint32_t end_temp = region.get_end_1() - shift;
            
            //Notify the user of the action
            if(verbose){cout << "MODIFY\t" << feat["type"] << "\t" << feat.m_gff_attributes["ID"] << " " << feat.m_gff_attributes["Name"] << endl;}
            
            //Modify the just the end of the feature
            region.set_end_1(end_temp);
            
            //Mark it as pseudo
            feat.flag_pseudo(verbose);
            
            //Modify the notes for this feature
            //if(feat["type"] != "region" && feat["type"] != "source" && !feat.m_gff_attributes.count("Mutation from " + mut_type))feat.m_gff_attributes["Note"].push_back("Mutation from " + mut_type);
          }
        }
        
        // Iterate it ONLY if we haven't erased region.
        if (it_region_iterate) it_region++;

      }
      
      // Now DELETE features that no longer have any regions!
      if (feat.m_locations.size() == 0) {
        
        //Notify the user of the action
        if(verbose){cout << "REMOVED\t" << feat["type"] << "\t" << feat.m_gff_attributes["ID"] << " " << feat.m_gff_attributes["Name"] << endl;}
        
        //Remove the current feature
        it_feature = m_features.erase(it_feature);
        
        //We just removed the current feature, do not iterate.
        it_feature_iterate = false;
        
      }
      
      // Iterate it ONLY if we haven't erased something.
      if (it_feature_iterate) it_feature++;
    }
    
    this->update_feature_lists();
  }
  
  // Inserts AFTER the input position
  // Place insertion_seq at first position after pos_1.
  // This function will shift everything else.
  // mut_type is used to append the type of mutation to the feature notes.
  // verbose outputs messages to console.
  // 
  // Successfully checks all three feature lists.  m_features, m_genes, and m_repeats.
  void cAnnotatedSequence::insert_sequence_1(int32_t pos_1, const string &insertion_seq, string mut_type, bool verbose)
  {
    (void) verbose;
    (void) mut_type;
    m_fasta_sequence.insert_sequence_1(pos_1, insertion_seq);
    
    //Variable for insertion length, only want to call the
    //function once
    uint32_t insert_length = insertion_seq.length();
    
    //Modify the length of the sequence
    m_length += insert_length;
    
    //Notify what mutation is being worked on
    //if(verbose)cout << "** " << mut_type << " **" << endl;
    
    //Iterate through all the features
    for (list<cSequenceFeaturePtr>::iterator it_feature = m_features.begin(); it_feature != m_features.end(); it_feature++)
    {
      //The current feature we're looking at
      cSequenceFeature& feat = **it_feature;
      
      // Iterature through all regions of the feature
      for (cFeatureLocationList::iterator it_region = feat.m_locations.begin(); it_region != feat.m_locations.end(); it_region++)
      {
        cFeatureLocation& region = *it_region;
         
        //Does the region end after the insertion?
        //If it ends on the same postion, do nothing
        //because we're adding it AFTER.
        if(region.get_end_1() > pos_1)
        {
          //Does the feature start after the insertion?
          //Starting on the same postion will mean we
          //do NOT alter the start postion.
          if(region.get_start_1() > pos_1)
          {                
            //Notify the user of the upcomming action
            //if(verbose){cout << "SHIFT\t" << feat["type"] << "\t" << feat.m_gff_attributes["ID"] << " " << feat.m_gff_attributes["Name"] << endl;}
            
            //Shift the entire feature down the line
            region.set_start_end_1(region.get_start_1() + insert_length, region.get_end_1() + insert_length);
          }
          else //If we can't move the start, only move the end.  This is a modification of the feature
          {
            //Temporary variable for the new end position
            uint32_t end_temp = region.get_end_1() + insert_length;
            
            //Notify the user of the action
            //if(verbose){cout << "MODIFY\t" << feat["type"] << "\t" << feat.m_gff_attributes["ID"] << " " << feat.m_gff_attributes["Name"] << endl;}
            
            //Modify the end position
            region.set_end_1(region.get_end_1() + insert_length);
            
            //Mark it as pseudo
            feat.flag_pseudo(verbose);
            
            //Modify the notes for this feature
            //if(feat["type"] != "region" && feat["type"] != "source" && !feat.m_gff_attributes.count("Mutation from " + mut_type))feat.m_gff_attributes["Note"].push_back("Mutation from " + mut_type);
          }            
        }
      }
    }
    
    this->update_feature_lists();

  }
  
  
  typedef enum{OUTSIDE=0, INSIDE, OVERLAP_START, OVERLAP_END, OVERLAP_BOTH} overlap_region_type;
  
  // Inverts the sequence from start_1 to end_1
  // Any features overlapping the ends are split into two and pseudo'ed
  void cAnnotatedSequence::invert_sequence_1(int32_t start_1, int32_t end_1, string mut_type, bool verbose)
  {
    (void) verbose;
    (void) mut_type;
    
    string inv_seq = reverse_complement(get_sequence_1(start_1, end_1));
    m_fasta_sequence.replace_sequence_1(start_1, end_1, inv_seq);
    
    //Iterate through all the features
    for (list<cSequenceFeaturePtr>::iterator it_feature = m_features.begin(); it_feature != m_features.end(); )
    {
      //The current feature we're looking at
      cSequenceFeature& feat = **it_feature;
      bool advance_it_feature = true;
      
      if (!feat.is_source()) {
      
        // Iterature through all regions of the feature
        // classify them as (outside, overlapping endpoints, or inside of the inversion)
        list<overlap_region_type> region_type_list;
        overlap_region_type consensus_region_type(OUTSIDE);
      
        for (cFeatureLocationList::iterator it_region = feat.m_locations.begin(); it_region != feat.m_locations.end(); it_region++)
        {
          bool overlap_start(false);
          bool overlap_end(false);
          
          cFeatureLocation& region = *it_region;
          overlap_region_type this_region_type(OUTSIDE);
          
          if ((region.get_start_1() >= start_1) && (region.get_end_1() <= end_1))
            this_region_type = INSIDE;
          else if (((region.get_end_1() >= start_1) && (region.get_end_1() <= end_1)) && ((region.get_start_1() >= start_1) && (region.get_start_1() <= end_1)))
            this_region_type = OVERLAP_BOTH;
          else if ((region.get_end_1() >= start_1) && (region.get_end_1() <= end_1))
            this_region_type = OVERLAP_END;
          else if ((region.get_start_1() >= start_1) && (region.get_start_1() <= end_1))
            this_region_type = OVERLAP_START;
          
          region_type_list.push_back(this_region_type);
          consensus_region_type = max(consensus_region_type, this_region_type);
        }
        
        // Now decide what to do as far as making a new feature
        // --> if it was all outside, then we don't do anything
        if (consensus_region_type != OUTSIDE) {
        
          // We make a pseudogene and connect the regions in the correct order and
          // orientation that the old reading frame would still be in this order
          cFeatureLocationList old_locations = feat.m_locations;
          feat.m_locations.clear();
          
          overlap_region_type last_region_type = region_type_list.front();
          
          list<overlap_region_type>::iterator region_type_list_it = region_type_list.begin();
          for (cFeatureLocationList::iterator it_region = old_locations.begin(); it_region != old_locations.end(); it_region++) {
            
            if (*region_type_list_it == OUTSIDE) {
              feat.add_location(static_cast<cLocation>(*it_region));
            } else if (*region_type_list_it == INSIDE) {
              
              // Add a new inverted location
              feat.add_location(static_cast<cLocation>(*it_region));
              feat.m_locations.back().invert_within_region(start_1, end_1);
              
            } else if (*region_type_list_it == OVERLAP_END) {
              
              // We can't add split repeat regions
              if (feat.is_repeat()) {
                this->m_features.erase(it_feature);
                advance_it_feature = false;
              } else {
                feat.add_location(static_cast<cLocation>(*it_region));
                feat.m_locations.back().set_end_1(start_1-1);
                
                feat.add_location(static_cast<cLocation>(*it_region));
                feat.m_locations.back().set_start_1(start_1);
                feat.m_locations.back().invert_within_region(start_1, end_1);
              }
              
            } if (*region_type_list_it == OVERLAP_START) {
              
              // We can't add split repeat regions
              if (feat.is_repeat()) {
                this->m_features.erase(it_feature);
                advance_it_feature = false;
              } else {
                feat.add_location(static_cast<cLocation>(*it_region));
                feat.m_locations.back().set_end_1(end_1);
                feat.m_locations.back().invert_within_region(start_1, end_1);
                
                feat.add_location(static_cast<cLocation>(*it_region));
                feat.m_locations.back().set_start_1(end_1+1);
              }

            } if (*region_type_list_it == OVERLAP_BOTH) {
              
              if (feat.is_repeat()) {
                this->m_features.erase(it_feature);
                advance_it_feature = false;
              } else {
                // Add three locations
                feat.add_location(static_cast<cLocation>(*it_region));
                feat.m_locations.back().set_end_1(start_1-1);

                feat.add_location(cLocation(start_1, end_1, -it_region->get_strand() ));
                feat.m_locations.back().set_start_end_1(start_1, end_1);
                
                feat.add_location(static_cast<cLocation>(*it_region));
                feat.m_locations.back().set_end_1(end_1+1);
                
                feat.flag_pseudo();
              }
            }
          }
          
          region_type_list_it++;
        }
      }
      if (advance_it_feature) it_feature++;
    }
    
    this->update_feature_lists();
  }

  // Repeat features within the given interval, on the given strand,
  // starting at the specified position (not after it). The sequence 
  // for these features must already have been inserted.
  
  void cAnnotatedSequence::repeat_feature_1(int32_t pos, int32_t start_del, int32_t end_del, cReferenceSequences& repeated_ref_seq_info, string& repeated_seq_id, int8_t strand, cLocation &repeated_region, bool verbose)
  {
    (void) verbose;
    //verbose = true;
    
    int32_t new_start = pos;
    int32_t new_end = pos + repeated_region.get_end_1() - repeated_region.get_start_1();
    int32_t adjusted_for_del_start = new_start;
    int32_t adjusted_for_del_end   = new_end - end_del - start_del;

    if (verbose) {
      cout << endl << ">> repeat_feature_1" << endl;  
      cout << "Moving interval: start " << repeated_region.get_start_1() << "-" << repeated_region.get_end_1() << endl; 
      cout << "To     interval: start " << new_start << "-" << new_end << endl;
      cout << "To     adjusted: start " << adjusted_for_del_start << "-" << adjusted_for_del_end << endl;
    }
        
    cSequenceFeatureList& features = repeated_ref_seq_info[repeated_seq_id].m_features;
    cSequenceFeatureList feat_list_new;        
           
    //Iterate through ALL the features in the ORIGINAL reference sequence info (not the updated one)
    for (list<cSequenceFeaturePtr>::iterator it_feature = features.begin(); it_feature != features.end(); it_feature++)
    {
      //The current feature we're looking at
      cSequenceFeature& feat = **it_feature;
      
      list<cLocation> overlapping_regions;
      bool overlapping_is_pseudogene = false;
      
      // Iterature through all regions of the feature
      for (cFeatureLocationList::iterator it_region = feat.m_locations.begin(); it_region != feat.m_locations.end(); it_region++)
      {
        cFeatureLocation& region = *it_region;

        // Which regions of the feature start and end inside of the repeat?
        
        if(  (region.get_start_1() >= repeated_region.get_start_1())
          && (region.get_end_1() <= repeated_region.get_end_1())
          && (!feat.is_source()) )
        {
          // Bona fide copy
          cLocation region_new = *it_region;

          // Depending on the strand of the mutation, relative to the
          // copy that we pulled, juggle the starts and ends here.
          // Note: start_del and end_del do not depend on the strand!! 
          //   (they are always in lowest coords, the highest coords)
          
          if(strand * region.get_strand() < 0) {
            //Set start to position plus the difference between the repeat feature's start, and this feature's start
            //Set end to start plus feature length
            int32_t start_1 = pos + (repeated_region.get_end_1() - region.get_end_1()) - start_del;
            int32_t end_1 = start_1 + (region.get_end_1() - region.get_start_1());
            region_new.set_start_end_1(start_1, end_1);
            // We are on the opposite strand
            region_new.set_strand( -region.get_strand());
          } else {
            //Set start to position plus the difference between the repeat feature's start, and this feature's start
            //Set end to start plus feature length
            int32_t start_1 = pos + (region.get_start_1() - repeated_region.get_start_1()) - start_del;
            int32_t end_1 = start_1 + (region.get_end_1() - region.get_start_1());
            region_new.set_start_end_1(start_1, end_1);
            //This is actually already true from the clone, but put here to make it obvious
            region_new.set_strand( region.get_strand() );
          }
          
          //If the feature start got shifted below the repeat start, set the start to the pos and flag it as pseudo
          if( region_new.get_start_1() < adjusted_for_del_start)  {
            region_new.set_start_1( adjusted_for_del_start );
            overlapping_is_pseudogene = true;
          }
          //Do we have an end_del? Does this feature end in an area that got deleted?
          if( region_new.get_end_1() > adjusted_for_del_end)  {
            region_new.set_end_1( adjusted_for_del_end );
            overlapping_is_pseudogene = true;
          }
          overlapping_regions.push_back(region_new);
        }
      }
        
      // If any of the regions overlapped, we are adding
      if (overlapping_regions.size() > 0) {
        
        // It's a pseudogene unless it is complete
        overlapping_is_pseudogene = overlapping_is_pseudogene || (overlapping_regions.size() != feat.m_locations.size());
        
        // Create a brand new feature, that we can love and cuddle.
        // This is where we copy the feature and all the attributes.
        cSequenceFeaturePtr fp(new cSequenceFeature(feat));
        
        // NOTE: You must FIX the gene names so that they are not identical after this call, this happens after APPLY is complete
        
        if (overlapping_is_pseudogene)
          fp->flag_pseudo();
        fp->m_locations.clear();
        
        if (verbose) cout << "  New Feature: " << (*fp)["type"] << (fp->m_pseudo ? " pseudo" : "") << endl;
        
        for (list<cLocation>::iterator it_loc=overlapping_regions.begin(); it_loc !=overlapping_regions.end(); it_loc++) {
          fp->add_location(static_cast<cLocation>(*it_loc));
          if (verbose) cout << " " << it_loc->get_start_1() << "-" << it_loc->get_end_1() << " strand " << it_loc->get_strand() << endl;
        }
        feat_list_new.push_back(fp);
      }
    }
    
    //cout << "Number of features before: " << m_features.size() << " " << m_genes.size() << " " << m_repeats.size() << endl;
    
    // Add these to all the feature list only now, at the end, so that we don't duplicate duplicate them
    for (cSequenceFeatureList::reverse_iterator itr_feat = feat_list_new.rbegin(); itr_feat != feat_list_new.rend(); itr_feat++) {
      cSequenceFeature& feat = **itr_feat;
      this->m_features.push_back(*itr_feat);
    }
    
    //cout << "Number of features after: " << m_features.size() << " " << m_genes.size() << " " << m_repeats.size() << endl;
   
    this->update_feature_lists();
  }
  
  //>! This function should be called after rearranging or changing the feature lists
  //   such as on first loading or after an APPLY
  void cAnnotatedSequence::update_feature_lists() {
    
    // The assumption here is that the feature list is correct,
    // but all other lists need to be regenerated from it.
      
    // Sort the list
    this->m_features.sort();
    
    // Regenerate repeat and gene lists
    this->m_genes.clear();
    this->m_repeats.clear();
    
    this->m_gene_locations.clear();
    this->m_repeat_locations.clear();
    
    for (cSequenceFeatureList::iterator itf = this->m_features.begin(); itf != this->m_features.end(); itf++ ) {
      cSequenceFeaturePtr& feat_p = *itf;
      
      // This is a double-check that features with indeterminate starts and ends are marked
      // pseudo so that we never have to worry about dealing with accidentally translating them
      if (feat_p->start_is_indeterminate() && feat_p->end_is_indeterminate())
        feat_p->flag_pseudo();
      
      ASSERT(feat_p->m_locations.size() != 0, "Feature lacks location: " + (*feat_p)["accession"] );
      
      for (cFeatureLocationList::iterator itl = feat_p->m_locations.begin(); itl != feat_p->m_locations.end(); itl++ ) {
          ASSERT(itl->is_valid(), "Feature has invalid location: " + (*feat_p)["accession"] + "\n" + itl->as_string())
      }
      
      if ( ((*feat_p)["type"] == "repeat_region") || ((*feat_p)["type"] == "mobile_element") )
      {
        this->m_repeats.push_back(feat_p);
        ASSERT(feat_p->m_locations.size() == 1, "Repeat feature must have only one location: " + (*feat_p)["accession"] + "\nPlease rename the duplicates so that they have unique locus_tags & names, e.g., gene_1 and gene_2" );
        
        this->m_repeat_locations.insert(this->m_repeat_locations.end(), feat_p->m_locations.begin(), feat_p->m_locations.end());
      }
      else if ( ((*feat_p)["type"] == "CDS") || ((*feat_p)["type"] == "tRNA") || ((*feat_p)["type"] == "rRNA") || ((*feat_p)["type"] == "RNA") || ((*feat_p)["type"] == "ncRNA") )
      {
        this->m_genes.push_back(feat_p);
        this->m_gene_locations.insert(this->m_gene_locations.end(), feat_p->m_locations.begin(), feat_p->m_locations.end());
      }
      
    }
    
    // Sort everything
    
    this->m_genes.sort();
    this->m_repeats.sort();
    
    this->m_gene_locations.sort();
    this->m_repeat_locations.sort();
  }
  
  // Load a complete collection of files and verify that sufficient information was loaded
  void cReferenceSequences::LoadFiles(const vector<string>& file_names)
  {
    list<string> sorted_unique_file_names(file_names.begin(), file_names.end());

    sorted_unique_file_names.unique();//Removes non-unique file names
    m_seq_id_to_original_file_name.clear(); // Clear old file names
    
    for(list<string>::const_iterator it = sorted_unique_file_names.begin(); it != sorted_unique_file_names.end(); it++)
    {
      this->PrivateLoadFile(*it);
    }
    
    this->VerifySequenceFeatureMatch();
    this->m_initialized = true;
    
    // To uppercase and change nonstandard chars to 'N' in all sequences.
    for (size_t i=0; i<this->size(); i++) {
      
      // Uppercase
      string this_sequence = to_upper((*this)[i].m_fasta_sequence.get_sequence());
      
      // Remedy nonstandard characters to 'N'
      map<char,uint32_t> bad_char;
      for (size_t j=0; j<this_sequence.size(); j++) {
        if ( !strchr( "ATCGN",  this_sequence[j] )) {
          if (bad_char.count(this_sequence[j])) {
            bad_char[this_sequence[j]]++;
          } else {
            bad_char[this_sequence[j]] = 1;
          }
          this_sequence[j] = 'N';
        }
      }
      
      (*this)[i].m_fasta_sequence.set_sequence(this_sequence);
      
      // Found some bad characters...
      if (bad_char.size()) {
        string warning_string("Non-standard base characters found in sequence: " + (*this)[i].m_seq_id + "\n");
        for(map<char,uint32_t>::iterator it = bad_char.begin(); it != bad_char.end(); it++) {
          warning_string += "  character: '" + to_string<char>(it->first) + "'  (occurrences: " + to_string(it->second) + ")\n";
        }
        warning_string +="Characters that are not in the set 'ATCGN' will be changed to 'N'.";
        WARN(warning_string);
      }
    }
    
    // Make certain feature items safe for making into lists separated by semicolons
    this->make_features_safe();
    
    // Finally, update feature lists
    this->update_feature_lists();
  }
  
  
  void cReferenceSequences::PrivateLoadFile(const string& file_name)
  {
    ifstream in(file_name.c_str());
    ASSERT(in.good(), "Could not open reference file: " +file_name);
    
    str_uint old_load = m_seq_id_loaded;
    map<string,pair<uint32_t,uint32_t> > old_seq_info;
    for(vector<cAnnotatedSequence>::iterator its = this->begin(); its != this->end(); its++)
    {      
      old_seq_info[(*its).m_seq_id].first = (*its).m_features.size();
      old_seq_info[(*its).m_seq_id].second = get_sequence_length((*its).m_seq_id);
    }

    //! Step 1: Determine what file format it is from first line
    string first_line;
    getline(in, first_line);
    in.close();

    vector<string> split_first_line = split_on_whitespace(first_line);
    FileType file_type = UNKNOWN;
    // Fasta?
    if (first_line[0] == '>') {
      file_type = FASTA;
    }
    // GenBank?
    else if (first_line.find("LOCUS") != string::npos) {
      file_type = GENBANK;
    }
    // GFF?
    else if (first_line.find("##gff-version 3") != string::npos) {
      file_type = GFF3;
    }
    // BULL? Lines have three "words"
    else if (split_first_line.size() == 3) {
      file_type = BULL;
    }
    // The file was not identified and will most likely cause Breseq to fail.
    else if(file_type == UNKNOWN) {
      ERROR("Could not determine format of reference file: " + file_name);
    }
    //! Step 2: Load appropriate file
    
    switch (file_type) {
      case GENBANK:
      {
        ifstream gbk_in(file_name.c_str());
        ASSERT(in.good(), "Could not open GenBank file: " +file_name);
        string line;
        bool eof_found = false;
        while (!gbk_in.eof() && getline(gbk_in,line))
        {
          if(GetWord(line) == "//")  {
            eof_found = true;  }
        }
        
        ASSERT(eof_found, file_name + "\nTHIS GENBANK FILE APPEARS TO BE INCOMPLETE.\nMAKE SURE YOU DOWNLOADED THE ENTIRE FILE.\nFILE NEEDS TO END WITH '//'.");
        
        ReadGenBank(file_name);
      }break;
        
      case FASTA:
      {
        ReadFASTA(file_name);
      }break;
        
      case BULL:
      {
        ReadBull(file_name);
      }break;
        
      case GFF3:
      {
        ReadGFF(file_name);
      }break;
        
      default:
        WARN("Could not load the reference file: " +file_name);
    }
  }
  
  void cReferenceSequences::VerifySequenceFeatureMatch()
  {
    bool Error = false;
    stringstream ss;    
    for (vector<cAnnotatedSequence>::iterator itr= this->begin(); itr != this->end(); itr++) {
      cAnnotatedSequence& as = *itr;
      if (!as.get_sequence_length() || !as.m_sequence_loaded_from_file.size()) {
        ss << "No sequence found for reference: " << as.m_seq_id << endl;
        ss << endl;
        ss << "Make sure that there is a sequence present in each file you are loading" << endl;
        ss << "or load the sequence for this reference from a separate FASTA file." << endl;
        ss << "This error is commonly caused by using a GenBank file which only has features." << endl;
        Error = true;
      }
      else if ((uint32_t)as.m_length != as.get_sequence_length())  {
        ss << "Contradictory lengths given in feature file and sequence file for reference: " << as.m_seq_id << endl;
        ss << (uint32_t)as.m_length << "  VS  " << as.get_sequence_length() << " (Sequence Length)" << endl;
        Error = true;
      }
      
    }
    if (Error) ERROR(ss.str());
    if (this->empty()) ERROR("Reference files were not loaded");
  }

  void cReferenceSequences::ReadFASTA(const string &file_name) {
    
    cFastaFile ff(file_name, ios_base::in);
    cFastaSequence on_seq;

    uint32_t on_seq_id = this->size();

    while ( ff.read_sequence(on_seq) ) {
      
      on_seq.set_name(safe_seq_id_name(on_seq.get_name()));
      
      // @JEB sorting the input files by type seems like a better way of doing this,
      //    but we can't do it just by looking at file name endings...
      //
      // If we have a dummy sequence id from loading a BULL feature file before a FASTA,
      // then fill it in and update the index
      if (m_seq_id_to_index.count(BULL_DUMMY_SEQ_ID)) {
        uint32_t seq_index = this->m_seq_id_to_index[BULL_DUMMY_SEQ_ID];
        this->m_seq_id_to_index.erase(BULL_DUMMY_SEQ_ID);
        (*this)[seq_index].m_seq_id = on_seq.get_name();
        m_seq_id_to_index[(*this)[seq_index].m_seq_id] = seq_index;       
      }
      else {
        this->add_new_seq(on_seq.get_name(), file_name);
      }
            
      // copy the info over (could define an assignment operator...)
      cAnnotatedSequence& this_seq = (*this)[on_seq.get_name()];
      this_seq.m_fasta_sequence = on_seq;
      this_seq.m_seq_id = on_seq.get_name();
      this_seq.m_description = on_seq.get_description();
      this_seq.m_length = on_seq.get_sequence_length();
      this_seq.set_sequence_loaded_from_file(file_name);
    }
  }
    
  void cReferenceSequences::ReadFASTA(cFastaFile& ff) 
  {
    cFastaSequence on_seq;

    while ( ff.read_sequence(on_seq) ) {
      
      on_seq.set_name(safe_seq_id_name(on_seq.get_name()));

      this->add_new_seq(on_seq.get_name(), ff.m_file_name);
      cAnnotatedSequence& this_seq = (*this)[on_seq.get_name()];
      this_seq.m_fasta_sequence = on_seq;
      this_seq.m_seq_id = on_seq.get_name();
      this_seq.m_length = on_seq.get_sequence_length();
      this_seq.set_sequence_loaded_from_file(ff.m_file_name);
    }

  }

  void cReferenceSequences::WriteFASTA(const std::string &file_name) {
        
    cFastaFile ff(file_name, ios_base::out);
    for(vector<cAnnotatedSequence>::iterator it_as = this->begin(); it_as < this->end(); it_as++) {
      if(it_as->m_length)ff.write_sequence(it_as->m_fasta_sequence);
    }
  }

  void cReferenceSequences::WriteFASTA(cFastaFile& ff) {
    
    for(vector<cAnnotatedSequence>::iterator it_as = this->begin(); it_as < this->end(); it_as++) {
      if(it_as->m_length)ff.write_sequence(it_as->m_fasta_sequence);
    }
  }

  /*! ReadGFF abides by the following format:
   *  http://www.sequenceontology.org/gff3.shtml 
   */
  void cReferenceSequences::ReadGFF(const string& file_name)
  {
    cFastaFile in(file_name.c_str(), ios_base::in);
    ASSERT(!in.fail(), "Could not open GFF file: " + file_name);
    //! Step 1: Header //! cFastaFile removes first line, so no header...
    string line;
    map<string, vector<cSequenceFeature*> > id_features_table; 
    while (!in.eof() && getline(in,line)) {
      cSequenceFeaturePtr fp(new cSequenceFeature);
      cSequenceFeature& feature = *fp;
      
      RemoveLeadingTrailingWhitespace(line);

  //! Step 2: Check for GFF Tags, reads FASTA from here.
      // We are concerned about a couple of ## GFF tags
      if (line[0] == '#') {
        if (line.find("##species") != string::npos) {
          //TODO @GRC
          continue;
        }
        else if (line.find("##sequence-region") == 0) {
          // Line of form ##sequence-region seqid start end
          stringstream ls(line);
          string x, seq_id, start, end;
          ls >> x >> seq_id >> start >> end;
          seq_id = safe_seq_id_name(seq_id);
          this->add_new_seq(seq_id, file_name);
          (*this)[seq_id].m_length = from_string<uint32_t>(end);
          continue;
        }
        else if (line.find("##original-file-name") == 0) {
          // Line of form ##original-file-name seq_id file_name
          stringstream ls(line);
          string x, seq_id, original_file_name;
          ls >> x >> seq_id >> original_file_name;
          this->add_new_seq(seq_id, file_name);
          this->m_seq_id_to_original_file_name[seq_id] = original_file_name;
          continue;
        }
        // Find embedded fasta file
        else if (line.find("##FASTA") == 0) {
          /* Things admittedly get a bit hairy right here, you have to
            take the next line (The one with ">XXXXXX") and set it as the
            current line for cFastaFile, after you've read the fasta you
            can exit the function since nothing should exist after.
            */
          getline(in,line);
          in.set_current_line(line);
          this->ReadFASTA(in);
          continue;
        } 
        else if (line.find("##DNA") == 0) {
          
          /* Alternative way of specifying DNA sequence
           used by Geneious exporter, for example
           
           ##DNA seq_id
           ##AGCTTTTCATTCTGACTGCAACGGGCAATATGTCTCTGTG
           ##TGGATTAAAAAAAGAGTGTCTGATAGCAGCTTCTGAACTG
           ...
           ##end-DNA
           
           */
          string seq_id = line.substr(5);
          RemoveLeadingTrailingWhitespace(seq_id);
          if (!m_seq_id_to_index.count(seq_id))
            this->add_new_seq(seq_id, file_name);
          //ASSERT(this->m_seq_id_to_index[seq_id], "Attempt to load sequence for non-existent seq_id with line:\n" + line);
                 
          cAnnotatedSequence& this_seq = (*this)[seq_id];
          this_seq.m_fasta_sequence.set_sequence("");
          this_seq.m_fasta_sequence.set_name(seq_id);
          
          getline(in,line);
          RemoveLeadingTrailingWhitespace(line);
          
          string nucleotide_sequence;
          
          while (!in.eof() && (line.find("##end-DNA") != 0)) {

            ASSERT(line.find("##") == 0, "Expected beginning ## and sequence line, but found:\n" + line); 
            string add_seq = line.substr(2);
            RemoveLeadingWhitespace(add_seq);
            nucleotide_sequence += add_seq;
            getline(in,line);
            RemoveLeadingTrailingWhitespace(line);
          }
          this_seq.m_fasta_sequence.set_sequence(nucleotide_sequence);
          this_seq.m_length = this_seq.m_fasta_sequence.get_sequence_length();
          this_seq.set_sequence_loaded_from_file(file_name);

          continue;
        } else {
          continue;
        }
      }
  /*! Step 3: Split line on tabs("\t") until last column, grab last column until endl("\n"),
      the default for getline(). !*/
      
      string seq_id;      
      vector<string> split_line = split(line, "\t");
      ASSERT( (split_line.size() == 8) || (split_line.size() == 9), "Expected 8 or 9 tab-delimited columns for line:\n"+line );
      
      // Handle columns up to the last one, "attributes"
      // Column 1: "seqid"
      seq_id = split_line[0];
      // Column 2: "source"
      feature["source"] = split_line[1];
      // Column 3: "type"
      feature["type"] = split_line[2];
      // Column 4: "start"
      int32_t start = from_string<int32_t>(split_line[3]);
      // Column 5: "end"
      int32_t end = from_string<int32_t>(split_line[4]);
      // Column 6: "score"
      feature["score"] = split_line[5];
      // Column 7: "strand"
      string strand_s = split_line[6];
      int8_t strand(0);
      if (strand_s == "+")
        strand = 1;
      else if (strand_s == "-")
        strand = -1;
      
      // Column 8: "phase"
      feature["phase"] = split_line[7];
      // Column 9: "attributes"
      // Handle parsing the attributes
      
      // Be robust to this column not exising
      if (split_line.size()>=9) {
        vector<string> attributes = split(split_line[8], ";");

        //Split attribute's key and value by "="
        for (vector<string>::iterator itr = attributes.begin(); itr != attributes.end(); itr++) {
          string& attribute = *itr;
          vector<string> key_value = split(attribute,"=");
          string& key = key_value.front();
          string& value = key_value.back();
        //! Case 2: Multiple values for given key, split by ","
          feature.m_gff_attributes[key] = split(value, ",");
          // unescape special characters after splitting
          for (uint32_t i=0; i<feature.m_gff_attributes[key].size(); i++)  {
            feature.m_gff_attributes[key][i] = GFF3UnescapeString(feature.m_gff_attributes[key][i]);
          }
        }
      }
      
      
      // Load certain information into the main hash, so breseq knows to use it
      

      // "accession" loaded from fields with preference: (accession > locus_tag > ID > Alias)
      if (feature.m_gff_attributes.count("accession"))
        feature["accession"] = join(feature.m_gff_attributes["accession"], ",");
      else if (feature.m_gff_attributes.count("locus_tag"))
        feature["accession"] = join(feature.m_gff_attributes["locus_tag"], ",");
      else if (feature.m_gff_attributes.count("ID"))
        feature["accession"] = join(feature.m_gff_attributes["ID"], ",");
      else if (feature.m_gff_attributes.count("Alias"))
        feature["accession"] = join(feature.m_gff_attributes["Alias"], ",");
  
      // "name" loaded from fields with preference Set the name using fields: (Name > gene > feature["accession"])
      if (feature.m_gff_attributes.count("Name"))
        feature["name"] = join(feature.m_gff_attributes["Name"], ",");
      else if (feature.m_gff_attributes.count("gene"))
        feature["name"] = join(feature.m_gff_attributes["gene"], ",");
      else
        feature["name"] = feature["accession"];

      
      // "product" loaded from fields with preference: (product > Note)
      if (feature.m_gff_attributes.count("product"))
        feature["product"] = join(feature.m_gff_attributes["product"], ",");
      else if (feature.m_gff_attributes.count("Note"))
        feature["product"] = join(feature.m_gff_attributes["Note"], ",");
    
      // fCDS type indicates CDS that is pseudo => internally store as CDS with pseudo flag set
      if (feature["type"] == "fCDS") {
        //feature.m_pseudo = true; set by following code...
        feature.m_gff_attributes["Pseudo"] = make_vector<string>("true");
        feature["type"] = "CDS";
      }
      if (feature.m_gff_attributes.count("Pseudo"))
        feature.m_pseudo = from_string<bool>(feature.m_gff_attributes["Pseudo"][0]);
      
      // Load translation table over to the gene
      if (feature.m_gff_attributes.count("transl_table"))
        feature["transl_table"] = feature.m_gff_attributes["transl_table"][0];

      // Load indeterminate start/end coordinates according to out custom fields.
      bool start_is_indeterminate = false;
      bool end_is_indeterminate = false;
      if (feature.m_gff_attributes.count("indeterminate_coordinate")) {
        for (vector<string>::iterator it=feature.m_gff_attributes["indeterminate_coordinate"].begin(); it!=feature.m_gff_attributes["indeterminate_coordinate"].end(); it++) {
          if (*it == "start")
            start_is_indeterminate = true;
          else if (*it == "end")
            end_is_indeterminate = true;
        }
        // Erase this property because it will be added back
        feature.m_gff_attributes.erase("indeterminate_coordinate");
      }
      
      list<cLocation> locs = ((*this)[seq_id]).SafeCreateFeatureLocations(feature, start, end, strand, start_is_indeterminate, end_is_indeterminate);
      
      for(list<cLocation>::iterator it=locs.begin(); it!=locs.end(); it++) {
        feature.add_location(*it);
      }
      
      //! Step 4: Determine if sequence already exists (find or create if not found)
      this->add_new_seq(seq_id, file_name);
    
      // If this is a landmark "region" corresponding to the entire chromosome grab extra information
      if ((feature["type"] == "region") && (feature.m_locations.front().get_start_1() == 1) && (feature.m_locations.front().get_end_1() == (*this)[seq_id].m_length)) {
        if (feature.m_gff_attributes.count("Is_circular"))
          (*this)[seq_id].m_is_circular = (feature.m_gff_attributes["Is_circular"][0] == "true");

        if (feature.m_gff_attributes.count("Note"))
          (*this)[seq_id].m_description = feature.m_gff_attributes["Note"][0];
      }

      // Add feature / determine if it needs to be added as an additional region of a previous feature.
      // In this case, look for a sort_index field to properly add it
      if (feature.m_gff_attributes.count("ID")) {
        string id = feature.m_gff_attributes["ID"][0];
        
        bool is_sub_location = false;
        
        if (id_features_table.count(id))  {
          vector<cSequenceFeature*> features = id_features_table[id];
          
          //Search to see if 'type' matches also.
          for (uint32_t i = 0; i < features.size();  ++i) {
            if ((*features[i])["type"] == feature["type"]) {
              
              //Update cLocation to include this sublocation (or all sublocaitons for split across origin case)
              for(cFeatureLocationList::iterator it_add_r=feature.m_locations.begin(); it_add_r!=feature.m_locations.end(); it_add_r++) {
                features[i]->add_location(*it_add_r);
              }
              is_sub_location = true;
              break;
            }
          }
        }
        
        if (is_sub_location) {
          continue; //Don't add to feature lists.
        } else {
          id_features_table[id].push_back(&feature);
        }

      }

      (*this)[seq_id].m_features.push_back(fp);
      (*this)[seq_id].set_features_loaded_from_file(file_name, true); // features may be spread across multiple files
    }
  }
  
  
/*! WriteGFF abides by the following format:
  http://www.sequenceontology.org/gff3.shtml 
    Parent-child relationships are not encoded,
    Instead the "discontinuous feature" rules are used.
    Note: This puts pieces of CDS for a spliced gene out of position order
!*/
void cReferenceSequences::WriteGFF( const string &file_name, bool no_sequence) {

  cFastaFile out(file_name.c_str(), ios_base::out);

  ASSERT(!out.fail(), "Failed to open file " + file_name);
    
  //! Step 1: Header
  out << "##gff-version 3" << endl;

  for (vector<cAnnotatedSequence>::iterator it_as = this->begin(); it_as < this->end(); it_as++) {
    if(it_as->m_length)out << "##sequence-region" << "\t" << it_as->m_seq_id << "\t" << "1" << "\t" << it_as->m_length << endl;
  }
  
  for (vector<cAnnotatedSequence>::iterator it_as = this->begin(); it_as < this->end(); it_as++) {
    if(it_as->m_length)out << "##original-file-name" << "\t" << it_as->m_seq_id << "\t" << it_as->m_file_name << endl;
  }
  
  //! Step 2: Features
  for (vector<cAnnotatedSequence>::iterator it_as = this->begin(); it_as < this->end(); it_as++) {
    
    for (cSequenceFeatureList::iterator it = it_as->m_features.begin(); it != it_as->m_features.end(); it++) {
      cSequenceFeature& feat = **it;

      enum {SEQID = 0, SOURCE, TYPE, START_1, END_1, SCORE, STRAND, PHASE, ATTRIBUTES};
      vector<string> columns(9, ".");

      columns[SEQID] = it_as->m_seq_id;
      if (feat.count("source")) columns[SOURCE] = feat["source"];
      if (feat.count("type"))   columns[TYPE] = feat["type"];
      
      //pseudogene written as fCDS = fragment CDS
      if ((feat["type"] == "CDS") && (feat.m_pseudo)) {
        columns[TYPE] = "fCDS";
      }
      
      //Note: Will add start_1 and end_1 and strand  below.
      if (feat.count("score"))  columns[SCORE] = feat["score"];
      if (feat.count("phase"))  columns[PHASE] = feat["phase"];

      //Attributes
      vector<string> attributes;
      map<string,vector<string> >::const_iterator itr;
      for (itr = feat.m_gff_attributes.begin(); itr != feat.m_gff_attributes.end(); itr++) {
        const string& key = itr->first;
        const vector<string>& values = itr->second;
        
        vector<string> s;
        for (vector<string>::const_iterator it = values.begin(); it != values.end(); it++) {
          s.push_back(GFF3EscapeString(*it));
        }
        
        if(s.size())attributes.push_back(key + "=" + join(s, ","));
      }
      string attributes_column = join(attributes, ";");

      int32_t location_index = 1;
      for (cFeatureLocationList::const_iterator it = feat.m_locations.begin(); it != feat.m_locations.end(); it++ ) {
        
        const cFeatureLocation& region = *it;
        columns[START_1] = to_string(region.get_start_1());
        columns[END_1]   = to_string(region.get_end_1());
        columns[STRAND]  = (region.get_strand() == 1) ? "+" : "-";

        // Overly complicated way of writing out indeterminacy
        columns[ATTRIBUTES] = attributes_column;
        if (region.start_is_indeterminate() || region.end_is_indeterminate()) {
          columns[ATTRIBUTES] += (!attributes_column.empty() ? ";" : "");
          columns[ATTRIBUTES] += "indeterminate_coordinate=";
        }                          
        if (region.start_is_indeterminate())
          columns[ATTRIBUTES] += "start";  
        if (region.start_is_indeterminate() && region.end_is_indeterminate())
          columns[ATTRIBUTES] += ","; 
        if (region.end_is_indeterminate())
          columns[ATTRIBUTES] += "end";
        
        out << join(columns, "\t") << endl;
        location_index++;
      }


    }
  }
  
  if (!no_sequence) {
    //! Step 3: Fasta, one command writes out all sequences
    out << "##FASTA" << endl;
    this->WriteFASTA(out);
  }
  
  out.close();
}

  
  
// This only outputs the features. No sequence!
void cReferenceSequences::WriteCSV(const string &file_name) {
  ofstream out(file_name.c_str());
  out << join(make_vector<string>("seq_id")("start")("end")("strand")("feat_id")("feat_type")("gene")("desc"), ",") << endl;
  enum {SEQ_ID = 0, START_1, END_1, STRAND, FEAT_ID, FEAT_TYPE, GENE, DESCRIPTION, NUM_COLS};

  // iterate through sequences
  for (vector<cAnnotatedSequence>::iterator it_as = this->begin(); it_as < this->end(); it_as++) {
    
    // iterate through features
    for (cSequenceFeatureList::iterator it = it_as->m_features.begin(); it != it_as->m_features.end(); it++) {
      cSequenceFeature& feat = **it;
      
      vector<string> columns(NUM_COLS, "");
      
      columns[SEQ_ID] = double_quote(it_as->m_seq_id);
      if (feat.count("accession"))   columns[FEAT_ID] = double_quote(feat["accession"]);
      if (feat.count("type"))   columns[FEAT_TYPE] = double_quote(feat["type"]);
      if (feat.m_pseudo && (columns[FEAT_TYPE] == "CDS")) {
        columns[FEAT_TYPE] = "fCDS";
      }
      if (feat.count("name"))   columns[GENE] = double_quote(feat["name"]);
      if (feat.count("product"))   columns[DESCRIPTION] =  double_quote(feat["product"]);
      
      //Note: Adds start_1 and end_1 below.
      
      //Attributes
      for (cFeatureLocationList::const_iterator it = feat.m_locations.begin(); it != feat.m_locations.end(); it++ ) {
        
        const cFeatureLocation& region = *it;

        columns[START_1] = to_string(region.get_start_1());
        columns[END_1]   = to_string(region.get_end_1());
        columns[STRAND] = (region.get_strand() == 1) ? "1" : "-1";

        out << join(columns, ",") << endl;
      }
    }
  }
}

void cReferenceSequences::ReadGenBank(const string& in_file_name) {

  ifstream in(in_file_name.c_str(), ios_base::in);
  ASSERT(!in.fail(), "Could not open GenBank file: " + in_file_name);

  while (ReadGenBankFileHeader(in, in_file_name)) {
    
    cAnnotatedSequence& this_seq = this->back();
    
    // add a 'region' feature for GFF3 output
    cSequenceFeaturePtr f(new cSequenceFeature);
    (*f)["type"] = "region";
    
    ASSERT(this_seq.m_length != 0, "Did not find bp length in GenBank LOCUS Line for sequence: " + this_seq.m_seq_id);
    f->add_location(cLocation(1, this_seq.m_length, 1));
    
    if (this_seq.m_is_circular)
      f->m_gff_attributes["Is_circular"].push_back("true");
    else
      f->m_gff_attributes["Is_circular"].push_back("false");
    
    f->m_gff_attributes["Note"].push_back(this_seq.m_description);
    this_seq.m_features.push_back(f);
    
    ReadGenBankFileSequenceFeatures(in, this_seq);
    this_seq.set_features_loaded_from_file(in_file_name);
    
    ReadGenBankFileSequence(in, this_seq);   
    this_seq.set_sequence_loaded_from_file(in_file_name);
    
  }
}

bool cReferenceSequences::ReadGenBankFileHeader(ifstream& in, const string& file_name) {

  //std::cout << "header" << std::endl;
  string line;
  bool found_LOCUS_line = false;
  cAnnotatedSequence* s = NULL;
  while (!in.eof()) {
    breseq::getline(in, line);
    string first_word = GetWord(line);
    RemoveLeadingTrailingWhitespace(line);

    // This is the first line
    if (first_word == "LOCUS") {

      // Example line 
      // LOCUS       EB03                 4629813 bp    DNA     circular BCT 13-APR-2007
      
      string w;
      w = GetWord(line);
      string seq_id = w;
      seq_id = safe_seq_id_name(seq_id);
      
      this->add_new_seq(seq_id, file_name);
      s = &((*this)[seq_id]);
      
      w = GetWord(line);
      s->m_length = atoi(w.c_str());
      
      w = GetWord(line);
      w = GetWord(line);
      w = GetWord(line);
      if (to_lower(w) == "circular")
        s->m_is_circular = true;

      // Should only be one line like this per record!
      ASSERT(!found_LOCUS_line, "Multiple LOCUS lines found in single GenBank record."); 
      found_LOCUS_line = true;
    }

    if (first_word == "DEFINITION") {
      ASSERT(s, "Missing LOCUS line before DEFINITION line in GenBank record.");
      s->m_description = line;
    }

    if (first_word == "FEATURES") break;
  }

  return (found_LOCUS_line);
}

/*! Load start, end, strand for feature from a GenBank coordinate string.
 *
 *  The string may cover multiple lines. Currently we do not handle discontinuous features,
 *  and features that cross the origin of a circular chromosome are returned with end < start. 
 */
list<cLocation> cAnnotatedSequence::ReadGenBankCoords(const cSequenceFeature& in_feature, string& s, ifstream& in) {

// Typical coordinate strings:
//   1485..1928
//   complement(2644..3159)
//  
// Example from a circular genome:
//   complement(join(7504..7835,1..163))
//
  
  // Read through all parentheses and across multiple lines if necessary
  int32_t parentheses_level = 0;
  size_t parenthesis_pos = s.find_first_of("()");
  
  while(parenthesis_pos != string::npos) {
  if (s.at(parenthesis_pos) == '(') {
      parentheses_level++;
    } else {
      parentheses_level--;
    }
    parenthesis_pos = s.find_first_of("()", parenthesis_pos+1);
  }
    
  // Add in remaining lines
  string s2;
  while ((parentheses_level > 0) && !in.eof()) {
    breseq::getline(in, s2);
    RemoveLeadingTrailingWhitespace(s2);
  
    size_t on_pos = s2.find_first_of("()");
    while(on_pos != string::npos) {
  
      if (s2.at(on_pos) == '(') {
        parentheses_level++;
      } else {
        parentheses_level--;
      }
      on_pos = s2.find_first_of("()", on_pos+1);
    }
    s += s2;
  }
  ASSERT(parentheses_level == 0, "Unmatched parenthesis in feature location.");
  
  // Clean tab and space characters.
  s = substitute(s, " ", "");
  s = substitute(s, "\t", "");
  
  // Parse and add locations
  return ParseGenBankCoords(in_feature, s);
}
  
// Parses a sub-part of the full location string
list<cLocation> cAnnotatedSequence::ParseGenBankCoords(const cSequenceFeature& in_feature, string& s, int8_t in_strand)
{
  list<cLocation> locs;
  
  //Look for an operation.
  //complement().
  if (s.find("complement(") == 0) {
    uint32_t n = string("complement(").size();
    string value = s.substr(n, s.size() - n - 1);
    list<cLocation> sub_locs = ParseGenBankCoords(in_feature, value, in_strand);
    // Set all strands to opposite and reverse order
    for(list<cLocation>::iterator it = sub_locs.begin(); it != sub_locs.end(); it++) {
      it->set_strand(-it->get_strand());
    }
    sub_locs.reverse();
    locs.insert(locs.end(), sub_locs.begin(), sub_locs.end());
  }
  //join()
  else if (s.find("join(") == 0) {
    uint32_t n = string("join(").size();
    vector<string> tokens = split(s.substr(n, s.size() - n - 1), ",");
    
    int8_t consensus_strand = 0;
    for (uint32_t i = 0; i < tokens.size(); ++i) {
      list<cLocation> sub_locs = ParseGenBankCoords(in_feature, tokens[i], in_strand);
      locs.insert(locs.end(), sub_locs.begin(), sub_locs.end());
    }
    
    // Example of difficult lines:
    //
    // gene            complement(join(205502..452528,1..182513))
    // CDS             complement(join(205502..205689,207093..207665,
    //                 210083..210243,181155..181546,182361..182513))
    
  }
  else {
    //Split on .. (usually 2)
    vector<string> tokens = split(s, "..");
    
    // Handle < and > for indeterminate coords (and remove so atoi works)
    bool start_is_indeterminate = false;
    if (tokens.front()[0] == '<') {
      start_is_indeterminate = true;
      tokens.front().replace(0,1,"");
    }
    
    bool end_is_indeterminate = false;
    if (tokens.back()[0] == '>') {
      end_is_indeterminate = true;
      tokens.back().replace(0,1,"");
    }
    
    list<cLocation> new_locs = this->SafeCreateFeatureLocations(
                                                         in_feature,
                                                         atoi(tokens.front().c_str()),
                                                         atoi(tokens.back().c_str()),
                                                         in_strand,
                                                         start_is_indeterminate,
                                                         end_is_indeterminate
                                                         );
    locs.insert(locs.end(), new_locs.begin(), new_locs.end());
  }
  
  //ASSERT(loc.get_start_1() <= loc.get_end_1(), "Start coordinate is greater than end coordinate. Error parsing corrdinates:\n" + s);
  
  return locs;
}

// By "safe" we mean for circular genomes with problem features that need to be split
// 1) Considers locations that wrap around origin (when start > end)
// 2) Considers locations that extend past the end of the numbering wrapping around
list<cLocation> cAnnotatedSequence::SafeCreateFeatureLocations(
                                                        const cSequenceFeature& in_feature,
                                                        int32_t in_start_1,
                                                        int32_t in_end_1,
                                                        int8_t in_strand,
                                                        bool in_start_is_indeterminate,
                                                        bool in_end_is_indeterminate
  )
{
  // Unfortunately, we cannot count on this being initialized with information that would help with debugging
  (void)in_feature;
  
  list<cLocation> locs;
  
  // We need to check that the feature is not entirely outside of the reference
  if (m_is_circular) {
    if ((in_start_1 > this->m_length) && (in_end_1 > this->m_length) ) {
      ERROR("Error in reference file. Feature on sequence " + this->m_seq_id + " has coordinates (" + to_string(in_start_1) + "-" + to_string(in_end_1) + ") that are both outside of the circular sequence bounds (1-" + to_string(this->m_length) + ").");
    }
  } else {
    if ((in_start_1 > this->m_length) || (in_end_1 > this->m_length) ) {
      ERROR("Error in reference file. Feature on sequence " + this->m_seq_id + " has coordinates (" + to_string(in_start_1) + "-" + to_string(in_end_1) + ") that are outside of the sequence bounds (1-" + to_string(this->m_length) + ").");
    }
  }
  
    
  if (m_is_circular) {
    in_end_1 %= this->m_length;
    in_start_1 %= this->m_length;
    if (in_end_1 == 0) in_end_1 = this->m_length;
    if (in_start_1 == 0) in_start_1 = this->m_length;

  }
  
  // Normal case
  if (in_start_1 <= in_end_1) {
  
    locs.push_back(
                   cLocation(
                             in_start_1,
                             in_end_1,
                             in_strand,
                             in_start_is_indeterminate,
                             in_end_is_indeterminate
                             )
                   );

    
  } else if (m_is_circular) {
    // extends around origin, add as two features
    // recall that end_1 < start_1
    
    locs.push_back(
                   cLocation(
                             1,
                             in_end_1,
                             in_strand,
                             false,
                             in_end_is_indeterminate
                             )
                   );
    
    locs.push_back(
                   cLocation(
                             in_start_1,
                             this->m_length,
                             in_strand,
                             in_start_is_indeterminate,
                             false
                            )
                   );
    
    // reverse if on the top strand!
    if (in_strand == 1) locs.reverse();
    
  } else {
    ERROR("Start coordinate (" + to_string<int32_t>(in_start_1) + ") must be less than or equal to end coordinate (" + to_string<int32_t>(in_end_1) + ") for feature that is not on a circular sequence.")
  }
  return locs;
}

  
// Examples of lines
//
// One-liner
//  /inference="ab initio prediction:tRNAscaN-SE:1.21"
//
// Example of nested parenthesis  
//  /anticodon=(pos:complement(1144371..1144373),aa:Arg,
//            seq:cct)
  
void cSequenceFeature::ReadGenBankTag(std::string& tag, std::string& s, std::ifstream& in) 
{
  // delete leading slash
  tag.erase(0,1);

  // erase through the equals on this line
  int pos = s.find("=");
  s.erase(0,pos+1);
  
  
  // Two cases, we encounter a quote or open parenthesis first (there should be no space before either).
  if (s[0] == '"') {
    string value;
    s.erase(0,1);
    
    size_t second_quote_pos = s.find("\"");
    
    // Skip cases of two quotes in a row, replacing with a single quote
    while ((second_quote_pos != string::npos) && (second_quote_pos+1 < s.length())) {
      if (s[second_quote_pos+1] != '"') break;
      value += s.substr(0, second_quote_pos+1);
      s.erase(0, second_quote_pos+2); // this erases second quote copy
      second_quote_pos = s.find("\"");
    }
    
    // One liner
    if (second_quote_pos != string::npos) {
      s.erase(second_quote_pos,s.length());
      value += s;
      (*this)[tag] = value;
      return;
    }
    
    // If the value is still quoted, we have to read additional lines until end quote
    value += s;
    
    bool found_last_quote = false;
    while (!found_last_quote && !in.eof()) {
      breseq::getline(in, s);
      RemoveLeadingTrailingWhitespace(s);
      
      second_quote_pos = s.find("\"");
      if (second_quote_pos != string::npos) {
        s.erase(second_quote_pos,s.length());
        found_last_quote = true;
      }
      
      if (tag != "translation") value += " ";
      value += s;
    }
    assert(found_last_quote);
    
    (*this)[tag] = value;
  }
  
  else if (s[0] == '(') {
    s.erase(0,1);
    uint32_t parenthesis_level = 1;
    parenthesis_level += std::count(s.begin(), s.end(), '('); 
    parenthesis_level -= std::count(s.begin(), s.end(), ')');
    string value = s;
    
    while (parenthesis_level && !in.eof()) {
      breseq::getline(in, s);
      RemoveLeadingTrailingWhitespace(s);
      parenthesis_level += std::count(s.begin(), s.end(), '('); 
      parenthesis_level -= std::count(s.begin(), s.end(), ')');
      value += s;
    }
    assert(parenthesis_level==0);
        
    // erase last closing parenthesis
    value.erase(value.length()-1,value.length());    
    (*this)[tag] = value;    
  } else {
    // No quotes, assume one liner
    (*this)[tag] = s;
    return;
  }
  
}

void cReferenceSequences::ReadGenBankFileSequenceFeatures(std::ifstream& in, cAnnotatedSequence& s) {
  //std::cout << "features" << std::endl;
  cSequenceFeature* current_feature(NULL);
  cSequenceFeatureList all_features; // make preliminary list then add once entries are complete
  string line;
  while (!in.eof()) {
    getline(in, line);
    
    //cout << line << endl;
    string first_word = GetWord(line);
    
    // Line was all whitespace. Code below requires skipping now.
    if (first_word.size() == 0) continue;
    
    //std::cout << first_word << "::" << line << std::endl;

    // Done with this section...
    if (first_word == "ORIGIN") break;

    // Major tag = new feature or information block
    if (first_word[0] != '/') {

      if (first_word != "BASE") {
        cSequenceFeaturePtr new_feature(new cSequenceFeature);
        all_features.push_back( new_feature );
        current_feature = new_feature.get();
        (*current_feature)["type"] = first_word;
        // parse the rest of the line
        std::string coord_s = GetWord(line);
        if (coord_s.size() == 0) {
          WARN("Error reading GenBank entry: coordinates expected at end of line. This line will be skipped:\n" + first_word + " " + coord_s);
          continue;
        }
        
        list<cLocation> locs = s.ReadGenBankCoords(*current_feature, coord_s, in);
        for(list<cLocation>::iterator it=locs.begin(); it!=locs.end(); it++) {
          current_feature->add_location(*it);
        }
        
      }
    }
    // Minor tag = information about current feature
    else {
      ASSERT(current_feature, "No current feature.");
      current_feature->ReadGenBankTag(first_word, line, in); // reads multi-line entries
    }
  }
  
  for (cSequenceFeatureList::iterator it = all_features.begin(); it != all_features.end(); it++) {
    cSequenceFeature& feature = **it;

    // common changes for any type
    // use /note as the product if there is not product
    if (feature.SafeGet("product") == "") {
      feature["product"] = feature.SafeGet("note");
    }
    
    if (feature.SafeGet("note") != "") {
      feature["name"] = feature.SafeGet("note");
    }

    if (feature["type"] == "repeat_region" || feature["type"] == "mobile_element") {

      // Give the repeat region a name if NOTHING else can be found
      feature["name"] = "repeat_region";
      
      if (feature.SafeGet("label") != "")  // Benchling exports "label"
        feature["name"] = feature.SafeGet("label");
      else if (feature.SafeGet("note") != "")
        feature["name"] = feature.SafeGet("note");
      
      // E. coli case:
      if (feature.count("mobile_element") || 
          feature.count("mobile_element_type"))
      {
        if (feature.count("mobile_element"))
          feature["name"] = feature["mobile_element"];
        if (feature.count("mobile_element_type"))
          feature["name"] = feature["mobile_element_type"];

        string& name = feature["name"];

        // remove prefix
        int pos = name.find("insertion sequence:");
        if (pos != -1)
          name.erase(pos,pos+19);

        // remove suffix if "IS(\d)}
        pos = name.find_first_of("0123456789");
        if (pos != -1) {
          int found = name.find_first_not_of("0123456789", pos);
          if (found != -1) {
            name.erase(found, name.length());
          }
        }
      }      

      // S. cerevisiae case
      if (feature.SafeGet("rpt_family") != "")
        feature["name"] = feature["rpt_family"];

      // Benchling export only has label
      
      //std::cerr << (*it).SafeGet("mobile_element") << " " << (*it).SafeGet("name") << std::endl;

      if (feature.SafeGet("product") == "")
        feature["product"] = "repeat region";
    }
    else // genes (anything not a repeat region)
    {
      feature["name"] = "unknown";
      
      // Add name from these fields in order of preference
      if (feature.SafeGet("gene") != "")
        feature["name"] = feature["gene"];
      else if (feature.SafeGet("locus_tag") != "")
        feature["name"] = feature["locus_tag"];
      else if (feature.SafeGet("label") != "")  // Benchling exports "label"
        feature["name"] = feature.SafeGet("label");
      else if (feature.SafeGet("note") != "")
        feature["name"] = feature["note"];
      
      if (feature.SafeGet("transl_table") != "")
        feature["transl_table"] = feature["transl_table"];
      
      // Need backups to deal with cases of unnamed genes

      //std::cerr << (*it).SafeGet("name") << " " << (*it).SafeGet("gene") << " " << (*it).SafeGet("locus_tag") << std::endl;

      feature["accession"] = feature.SafeGet("locus_tag");

      // Is this feature marked as pseudo?
      feature.m_pseudo = (feature.count("pseudo") != 0);
    }
        
    // transfer to GFF
    feature["phase"] = "0";
    if (feature.SafeGet("locus_tag") != "")
      feature.m_gff_attributes["ID"] = make_vector<string>(feature["locus_tag"]);
    if (feature.SafeGet("product") != "")
      feature.m_gff_attributes["Note"] = make_vector<string>(feature["product"]);
    if (feature.m_pseudo) 
      feature.m_gff_attributes["Pseudo"].push_back("true");
    if (feature.SafeGet("accession") != "")
      feature.m_gff_attributes["Alias"] = make_vector<string>(feature["accession"]);
    if (feature.SafeGet("name") != "")
      feature.m_gff_attributes["Name"] = make_vector<string>(feature["name"]);

    if (feature.SafeGet("transl_table") != "")
      feature.m_gff_attributes["transl_table"] = make_vector<string>(feature["transl_table"]);
    
    s.m_features.push_back(*it);
  }
}

void cReferenceSequences::ReadGenBankFileSequence(std::ifstream& in, cAnnotatedSequence& s) {

  s.m_fasta_sequence.set_name(s.m_seq_id);

  string line;
  string nucleotide_sequence;
  while (!in.eof()) {
    getline(in, line);
    string first_word = GetWord(line);
    RemoveLeadingTrailingWhitespace(line);

    //cerr << first_word << "::" << line << std::endl;

    if (first_word == "//") break;
    
    // Load, but skip spaces

    for(string::iterator i=line.begin(); i!=line.end(); ++i) {
      if (*i == ' ') continue;
      nucleotide_sequence += *i;
    }
  }
  s.m_fasta_sequence.set_sequence(nucleotide_sequence);
  
  //cout << s.m_sequence << std::endl;
}

void cReferenceSequences::ReadBull(const string& file_name) {

  // This should only work if we have zero or one reference sequence, because it does not
  // give a seq_id, we assume the FASTA before or the FASTA after is paired with it.
  ASSERT(this->size() <= 1, "Bull format currently only works with one reference sequence");

  // if we encounter the BULL file first, create an empty sequence
  if (this->size() == 0)
  {
    // if one already exists, throw an error
    ASSERT(!this->m_seq_id_to_index.count(BULL_DUMMY_SEQ_ID), 
           "Two BULL formatted feature files loaded in a row. Ambiguous assignment to FASTA files.");
    this->add_new_seq(BULL_DUMMY_SEQ_ID, file_name);
  }
    
  ifstream in(file_name.c_str());
  if(!in.good()) WARN("Could not open file:" + file_name);

  char line[10];

  cSequenceFeatureList all_features;

  while ( !in.eof() ) {

    string line;
    getline(in, line);
    vector<string> s = split_on_whitespace(line);

    if (s.size() == 0) continue;
    ASSERT(s.size() == 3, "Expected 3 words in line, found " + to_string(s.size()) + " in line:\n" + line);
    
    cSequenceFeaturePtr current_feature(new cSequenceFeature);
    
    int32_t start_1 = from_string<uint32_t>(s[0]);
    int32_t end_1 = from_string<uint32_t>(s[1]);
    int8_t strand = 1;
    
    if (start_1 > end_1) {
      ::swap(start_1, end_1);
      strand = -1;
    }

    
    current_feature->add_location(
                                  cLocation(
                                            start_1,
                                            end_1,
                                            strand
                                            )
                                  );
    
    // transfer to GenBank
    (*current_feature)["name"] = s[2];
    (*current_feature)["type"] = "CDS";
    (*current_feature)["product"] = "";
    (*current_feature)["accession"] = s[2];
    
    // transfer to GFF3
    (*current_feature)["ID"] = s[2];
    (*current_feature)["Note"] = "";
    (*current_feature)["Alias"] = s[2];
    (*current_feature)["Name"] = s[2];
    
    all_features.push_back(current_feature);
  }
  in.close();
  
  cAnnotatedSequence& s = this->back();
  for (cSequenceFeatureList::iterator it = all_features.begin(); it != all_features.end(); it++) {
    cSequenceFeature& feat = **it;
    s.m_features.push_back(*it);
  }
  s.set_features_loaded_from_file(file_name, true);
}

/*! 
 *  If repeat_region is provided, return a specific copy of the repeat.
 *  Otherwise, return a "typical" copy (the most common sequence and/or size for the element)
 *
 * If fatal_error is TRUE, then we die if we are unable to find the sequence
 * If fatal_error is FALSE, we return an empty string if we are unable to find the sequence
 */  
string cReferenceSequences::repeat_family_sequence(
                                                   const string &repeat_name, 
                                                   int8_t strand, 
                                                   string* repeat_region, 
                                                   string* picked_seq_id, 
                                                   cFeatureLocation* picked_sequence_feature,
                                                   bool fatal_error
                                                   )
{  
  bool verbose = false;
  
  if (verbose) {
    cout << "Finding repeat family sequence for: " << repeat_name << " on strand " << static_cast<int32_t>(strand) << endl;
    if (repeat_region) cout << "Specific repeat region requested: " << *repeat_region << endl;
  }
  
  cFeatureLocation* picked_rep(NULL);
  cAnnotatedSequence* picked_seq(NULL);
  
  map<string, uint32_t> repeat_sequence_count;
  map<string, uint32_t> repeat_sequence_pos;  
  map<uint32_t, uint32_t> repeat_size_count;  
  map<uint32_t, uint32_t> repeat_size_pos;
  
  // OPTION 1: REQUESTED REPEAT
  if (repeat_region) {
    
    uint32_t target_id;
    uint32_t start_pos_1, end_pos_1;
    parse_region(*repeat_region, target_id, start_pos_1, end_pos_1);
        
    cAnnotatedSequence& this_seq = (*this)[target_id];
    cFeatureLocationList& repeat_locations = this_seq.m_repeat_locations;
    
    for (cFeatureLocationList::iterator itr_rep = repeat_locations.begin(); itr_rep != repeat_locations.end(); itr_rep++) {
      cFeatureLocation& rep = *itr_rep;
      
      // Not the right family...
      if(rep.get_feature()->SafeGet("name") != repeat_name)
        continue;
      
      // We accept either seq_id:start-end or seq_id:end_start (for complement features),
      // but ignore which strand was provided and use the correct strand.
      if ((rep.get_start_1() == static_cast<int32_t>(start_pos_1)) && (rep.get_end_1() == static_cast<int32_t>(end_pos_1))) {
        picked_seq = &this_seq;
        picked_rep = &rep;
      }
      else if ((rep.get_end_1() == static_cast<int32_t>(start_pos_1)) && (rep.get_start_1() == static_cast<int32_t>(end_pos_1))) {
        picked_seq = &this_seq;
        picked_rep = &rep;
      }
    }
    
    ASSERT(picked_rep, "Could not find repeat of type [" + repeat_name + "] matching requested reference sequence region: " + *repeat_region);
  }
  
  // OPTION 2: TYPICAL REPEAT
  else {  
    // Loop through all reference sequences
    for (vector<cAnnotatedSequence>::iterator itr_seq = this->begin(); itr_seq != this->end(); itr_seq++) {
      cAnnotatedSequence& this_seq = *itr_seq;
      cFeatureLocationList& repeat_locations = this_seq.m_repeat_locations;
      
      for (cFeatureLocationList::iterator itr_rep = repeat_locations.begin(); itr_rep != repeat_locations.end(); itr_rep++) {
        cFeatureLocation& rep = *itr_rep;
        
        // Not the right family...
        if(rep.get_feature()->SafeGet("name") != repeat_name)
          continue;
        
        // Stores all the sequences of this family so we can compare them and pick the most "typical".
        string adjSeq = this_seq.get_sequence_1(rep.get_start_1(), rep.get_end_1());
        if (strand != rep.get_strand())
          adjSeq = reverse_complement(adjSeq);
        
        int32_t this_size = rep.get_end_1() - rep.get_start_1() + 1;
        repeat_size_count[this_size]++;   
        if (repeat_size_pos.count(this_size) == 0) {
          repeat_size_pos[this_size] = rep.get_start_1();
        }
        repeat_sequence_count[adjSeq]++;
        // We want to use the first one we encountered
        if (repeat_sequence_pos.count(adjSeq) == 0) {
          repeat_sequence_pos[adjSeq] = rep.get_start_1();
        }
      }
    }
    
    // This will set the region_pos to whichever sequence has the most copies.
    // If number of copies is not greater than 1, we will use the most common size instead.
    int32_t region_pos;
    if(repeat_sequence_count.size())
    {    
      if (max_element(repeat_sequence_count.begin(), repeat_sequence_count.end(), map_comp_second<string, uint32_t>)->second > 1) {
        region_pos = repeat_sequence_pos[max_element(repeat_sequence_count.begin(), repeat_sequence_count.end(), map_comp_second<string, uint32_t>)->first];  }
      else  {
        region_pos = repeat_size_pos[max_element(repeat_size_count.begin(), repeat_size_count.end(), map_comp_second<uint32_t, uint32_t>)->first];  }
    }
    
    // Loop through all reference sequences
    for (vector<cAnnotatedSequence>::iterator itr_seq = this->begin(); itr_seq != this->end(); itr_seq++) {
      cAnnotatedSequence& this_seq = *itr_seq;
      cFeatureLocationList& repeat_locations = this_seq.m_repeat_locations;
      
      for (cFeatureLocationList::iterator itr_rep = repeat_locations.begin(); itr_rep != repeat_locations.end(); itr_rep++) {
        cFeatureLocation& rep = *itr_rep;
        
        if(rep.get_feature()->SafeGet("name") != repeat_name)
          continue;
        
        if (region_pos == rep.get_start_1()) {
          picked_seq = &this_seq;
          picked_rep = &rep;
          break;
        }
      }
    }
    
    if (fatal_error) {
      ASSERT(picked_rep, "Could not find repeat of type [" + repeat_name + "] in reference sequences.\n");
    }
    else if (!picked_rep) {
      return "";
    }
  }
  
  
  string repeat_seq = picked_seq->get_sequence_1(picked_rep->get_start_1(), picked_rep->get_end_1());
  if (strand != picked_rep->get_strand())
    repeat_seq = reverse_complement(repeat_seq);
  
  // return values
  if (picked_seq_id)
    *picked_seq_id = picked_seq->m_seq_id;
  if (picked_sequence_feature)
    *picked_sequence_feature = *picked_rep;
  
  if (verbose) {
    cout << "Picked repeat region: " << *picked_seq_id << ":" << picked_sequence_feature->get_start_1() << "-" << picked_sequence_feature->get_end_1() << endl;
    cout << "Sequence: " << repeat_seq << endl;
  }
  
  return repeat_seq;  
}

/*! Find the closest edge of a repeat in the specified direction within the specified distance
    "Edge" is defined as within 'max_distance' of the start or end coordinate,
    unless include_interior_matches (off by default) is set, in which case matches
    in the interior count as being zero distance from the boundary
 */
cFeatureLocation* cReferenceSequences::find_closest_repeat_region_boundary(int32_t position,
                                                                             const cSequenceFeatureList& repeat_list,
                                                                             int32_t& max_distance,
                                                                             int32_t direction,
                                                                             bool include_interior_matches)
{
  cFeatureLocation* repeat_ptr(NULL);
  
  for (cSequenceFeatureList::const_iterator it = repeat_list.begin(); it != repeat_list.end(); ++it) {
    ASSERT((*it)->m_locations.size()!=0, "Repeat region with no locations found: " + join((*it)->m_gff_attributes["name"], ","));
    ASSERT((*it)->m_locations.size()==1, "Repeat regions cannot have multiple sublocations:");

    cFeatureLocation* test_repeat_ptr = &(*it)->m_locations.front();
    
    // Distance from the closest end of the repeat, given the direction we are moving
    int32_t test_distance = ((direction == -1) ? position - static_cast<int32_t>(test_repeat_ptr->get_end_1()) : static_cast<int32_t>(test_repeat_ptr->get_start_1()) - position);
  
    // If include_interior_matches, change negative distances less than size to zero distance
    if (include_interior_matches) {
      if ((test_distance < 0) && (-test_distance <= test_repeat_ptr->get_end_1() - test_repeat_ptr->get_start_1() + 1)) {
        test_distance = 0;
      }
    } else {
      test_distance = abs(test_distance);
    }
    
    // We want the closest one without going over that is within max_distance
    if ( (test_distance >= 0) && (test_distance <= max_distance) ) {
      
      // Same distance, then choose the LONGEST repeat region
      // Important for transposons where each end may be annotated with a contained repeat region
      if (repeat_ptr && (test_distance == max_distance)) {
        if (test_repeat_ptr->get_end_1() - test_repeat_ptr->get_start_1() + 1 < repeat_ptr->get_end_1() - repeat_ptr->get_start_1() + 1)
          continue;
      }
      repeat_ptr = test_repeat_ptr;
      max_distance = test_distance;
    }
  }
  return repeat_ptr;
}

/*! Returns the last feature encountered that overlaps a position
 */
cFeatureLocation* cReferenceSequences::get_overlapping_feature(cFeatureLocationList& feature_list, int32_t pos)
{
  cFeatureLocation* feature_ptr(NULL);
  for (cFeatureLocationList::iterator it = feature_list.begin(); it != feature_list.end(); ++it) {
    if ( it->distance_to_position(pos) == 0 ) {
      feature_ptr = &(*it);
    }
    
  }
  return feature_ptr;
}

  
/* This function is much less efficient than ideal
   but we need to set up a sorted list of regions
   or a binary tree based on regions to be more efficient
*/
 
void cReferenceSequences::find_nearby_genes(
                                            cFeatureLocationList& gene_list,
                                            int32_t pos_1,
                                            int32_t pos_2,
                                            vector<cFeatureLocation*>& within_genes,
                                            vector<cFeatureLocation*>& between_genes,
                                            vector<cFeatureLocation*>& inside_left_genes,
                                            vector<cFeatureLocation*>& inside_right_genes,
                                            cFeatureLocation*& prev_gene,
                                            cFeatureLocation*& next_gene
                                            )
{
  int32_t min_distance_to_prev = numeric_limits<int32_t>::max();
  int32_t min_distance_to_next = numeric_limits<int32_t>::max();
  
  for (cFeatureLocationList::iterator it = gene_list.begin(); it != gene_list.end(); ++it)
  {
    cFeatureLocation& region = *it;
    
    if (region.get_end_1() < pos_1) {
      if (pos_1 - region.get_end_1() < min_distance_to_prev) {
        min_distance_to_prev = pos_1 - region.get_end_1();
        prev_gene = &region;
      }
    }
    
    if (  ( region.distance_to_position(pos_1) == 0 )
       && ( region.distance_to_position(pos_2) == 0 ) )
    {
      within_genes.push_back(&region);
    }
    else if ( region.distance_to_position(pos_1) == 0 )
    {
      inside_left_genes.push_back(&region);
    }
    else if ( region.distance_to_position(pos_2) == 0 )
    {
      inside_right_genes.push_back(&region);
    }
    else if ( (region.get_start_1() >= pos_1) && (region.get_end_1() <= pos_2) )
    {
      between_genes.push_back(&region);
    }
    
    if (region.get_start_1() > pos_2) {
      if (region.get_start_1() - pos_2 < min_distance_to_next) {
        min_distance_to_next = region.get_start_1() - pos_2;
        next_gene = &region;
      }
    }
  }
}

// Retrieved from http://www.ncbi.nlm.nih.gov/Taxonomy/Utils/wprintgc.cgi on 04-21-2012
vector<string> cReferenceSequences::translation_tables = make_vector<string>
  ("") // 0 placeholder to make vector 1-indexed
  ("FFLLSSSSYY**CC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 1
  ("FFLLSSSSYY**CCWWLLLLPPPPHHQQRRRRIIMMTTTTNNKKSS**VVVVAAAADDEEGGGG") // 2
  ("FFLLSSSSYY**CCWWTTTTPPPPHHQQRRRRIIMMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 3
  ("FFLLSSSSYY**CCWWLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 4
  ("FFLLSSSSYY**CCWWLLLLPPPPHHQQRRRRIIMMTTTTNNKKSSSSVVVVAAAADDEEGGGG") // 5
  ("FFLLSSSSYYQQCC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 6
  ("") // 7 deleted
  ("") // 8 deleted
  ("FFLLSSSSYY**CCWWLLLLPPPPHHQQRRRRIIIMTTTTNNNKSSSSVVVVAAAADDEEGGGG") // 9
  ("FFLLSSSSYY**CCCWLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 10
  ("FFLLSSSSYY**CC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 11
  ("FFLLSSSSYY**CC*WLLLSPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 12
  ("FFLLSSSSYY**CCWWLLLLPPPPHHQQRRRRIIMMTTTTNNKKSSGGVVVVAAAADDEEGGGG") // 13
  ("FFLLSSSSYYY*CCWWLLLLPPPPHHQQRRRRIIIMTTTTNNNKSSSSVVVVAAAADDEEGGGG") // 14
  ("FFLLSSSSYY*QCC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 15
  ("FFLLSSSSYY*LCC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 16
  ("") // 17 deleted
  ("") // 18 deleted
  ("") // 19 deleted
  ("") // 20 deleted
  ("FFLLSSSSYY**CCWWLLLLPPPPHHQQRRRRIIMMTTTTNNNKSSSSVVVVAAAADDEEGGGG") // 21
  ("FFLLSS*SYY*LCC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 22
  ("FF*LSSSSYY**CC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 23
  ("FFLLSSSSYY**CCWWLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSSKVVVVAAAADDEEGGGG") // 24
;
  
  vector<string> cReferenceSequences::initiation_codon_translation_tables = make_vector<string>
  ("") // 0 placeholder to make vector 1-indexed
  ("FFLMSSSSYY**CC*WLLLMPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 1
  ("FFLLSSSSYY**CCWWLLLLPPPPHHQQRRRRMMMMTTTTNNKKSS**VVVMAAAADDEEGGGG") // 2
  ("FFLLSSSSYY**CCWWTTTTPPPPHHQQRRRRIIMMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 3
  ("FFMMSSSSYY**CCWWLLLMPPPPHHQQRRRRMMMMTTTTNNKKSSRRVVVMAAAADDEEGGGG") // 4
  ("FFLMSSSSYY**CCWWLLLLPPPPHHQQRRRRMMMMTTTTNNKKSSSSVVVMAAAADDEEGGGG") // 5
  ("FFLLSSSSYYQQCC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 6
  ("") // 7 deleted
  ("") // 8 deleted
  ("FFLLSSSSYY**CCWWLLLLPPPPHHQQRRRRIIIMTTTTNNNKSSSSVVVMAAAADDEEGGGG") // 9
  ("FFLLSSSSYY**CCCWLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 10
  ("FFLMSSSSYY**CC*WLLLMPPPPHHQQRRRRMMMMTTTTNNKKSSRRVVVMAAAADDEEGGGG") // 11
  ("FFLLSSSSYY**CC*WLLLMPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 12
  ("FFLMSSSSYY**CCWWLLLLPPPPHHQQRRRRIIMMTTTTNNKKSSGGVVVMAAAADDEEGGGG") // 13
  ("FFLLSSSSYYY*CCWWLLLLPPPPHHQQRRRRIIIMTTTTNNNKSSSSVVVVAAAADDEEGGGG") // 14
  ("FFLLSSSSYY*QCC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 15
  ("FFLLSSSSYY*LCC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 16
  ("") // 17 deleted
  ("") // 18 deleted
  ("") // 19 deleted
  ("") // 20 deleted
  ("FFLLSSSSYY**CCWWLLLLPPPPHHQQRRRRIIMMTTTTNNNKSSSSVVVMAAAADDEEGGGG") // 21
  ("FFLLSS*SYY*LCC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG") // 22
  ("FF*LSSSSYY**CC*WLLLLPPPPHHQQRRRRMIIMTTTTNNKKSSRRVVVMAAAADDEEGGGG") // 23
  ("FFLMSSSSYY**CCWWLLLMPPPPHHQQRRRRIIIMTTTTNNKKSSSKVVVMAAAADDEEGGGG") // 24
  ;
  
map<string,uint16_t> cReferenceSequences::codon_to_aa_index = make_map<string,uint16_t>
  ("TTT",  0)("TTC",  1)("TTA",  2)("TTG",  3)
  ("TCT",  4)("TCC",  5)("TCA",  6)("TCG",  7)
  ("TAT",  8)("TAC",  9)("TAA", 10)("TAG", 11)
  ("TGT", 12)("TGC", 13)("TGA", 14)("TGG", 15)
  ("CTT", 16)("CTC", 17)("CTA", 18)("CTG", 19)
  ("CCT", 20)("CCC", 21)("CCA", 22)("CCG", 23)
  ("CAT", 24)("CAC", 25)("CAA", 26)("CAG", 27)
  ("CGT", 28)("CGC", 29)("CGA", 30)("CGG", 31)
  ("ATT", 32)("ATC", 33)("ATA", 34)("ATG", 35)
  ("ACT", 36)("ACC", 37)("ACA", 38)("ACG", 39)
  ("AAT", 40)("AAC", 41)("AAA", 42)("AAG", 43)
  ("AGT", 44)("AGC", 45)("AGA", 46)("AGG", 47)
  ("GTT", 48)("GTC", 49)("GTA", 50)("GTG", 51)
  ("GCT", 52)("GCC", 53)("GCA", 54)("GCG", 55)
  ("GAT", 56)("GAC", 57)("GAA", 58)("GAG", 59)
  ("GGT", 60)("GGC", 61)("GGA", 62)("GGG", 63)
;

char cReferenceSequences::translate_codon(string seq, uint32_t translation_table, uint32_t codon_number_1, const string& gene)
{  
  ASSERT(seq.size()==3, "Attempt to translate codon without three bases" + (gene.size()>0 ? " in gene " + gene :"") + ".");
  ASSERT(translation_table <= cReferenceSequences::translation_tables.size(), "Unknown translation table #" + to_string(translation_table) + " requested." + (gene.size()>0 ? " for gene " + gene :"") + ".");
  const string& tt = (codon_number_1 == 1) 
    ? cReferenceSequences::initiation_codon_translation_tables[translation_table]
    : cReferenceSequences::translation_tables[translation_table]
  ;
  ASSERT(tt.size() == 64, "Provided translation table #" + to_string(translation_table) + " does not have 64 codons." + (gene.size()>0 ? " for gene " + gene :"") + ".");
  
  return (cReferenceSequences::codon_to_aa_index.count(seq) == 0) 
    ? '?' 
    : tt[cReferenceSequences::codon_to_aa_index[seq]];
}
  
char cReferenceSequences::translate_codon(string seq, string translation_table, string translation_table_1, uint32_t codon_number_1, const string& gene)
{  
  ASSERT(seq.size()==3, "Attempt to translate codon without three bases" + (gene.size()>0 ? " in gene " + gene :"") + ".");
  ASSERT(translation_table.size()==64, "Provided translation table #" + to_string(translation_table) + " does not have 64 codons." + (gene.size()>0 ? " for gene " + gene :"") + ".");
  const string& tt = (codon_number_1 == 1) ? translation_table_1 : translation_table;
  
  return (cReferenceSequences::codon_to_aa_index.count(seq) == 0) 
  ? '?' 
  : tt[cReferenceSequences::codon_to_aa_index[seq]];
}
  
string cReferenceSequences::translate_protein(cAnnotatedSequence& seq, cSequenceFeature& gene, string translation_table, string translation_table_1)
{
  // We don't translate a feature with an indeterminate start or end
  if (gene.start_is_indeterminate() || gene.end_is_indeterminate()) return "";
  
  // Go one codon at a time...    
  string on_codon;
  uint32_t on_codon_number_1 = 0;
  char on_aa = ' ';
  string nt_sequence = gene.get_nucleotide_sequence(seq);
  uint32_t on_nt_pos = 0;
  string protein_sequence;
  
  // This block protects against CDS that are not multiples of three (due to indeterminate ends)
  uint32_t dangling_nts = nt_sequence.length() % 3;
  if (dangling_nts != 0) {
    WARN("Attempt to translate a gene with a length that is not multiple of 3 nucleotides." + gene.get_locus_tag());
  }
  uint32_t max_nt_pos = nt_sequence.length();
  
  while (on_nt_pos < max_nt_pos) {
    
    on_codon = "";
    on_codon_number_1++;
    
    while (on_codon.size() < 3) {
      on_codon += nt_sequence[on_nt_pos];
      on_nt_pos++;
    }
    protein_sequence += translate_codon(on_codon, translation_table, translation_table_1, on_codon_number_1, gene.get_locus_tag());
    
  }
  
  return protein_sequence;
}
  
  

// This only tests mutations that partially overlap a gene or are contained within it!
// completely deleted genes are annotated outside of this function
bool cReferenceSequences::mutation_overlapping_gene_is_inactivating(const cDiffEntry& mut, const string& snp_type, const uint32_t start, const uint32_t end, const cGeneFeature& gene, const double inactivate_overlap_fraction)
{

  bool is_inactivating = false;

  // These coords are internal to the gene that was mutated (accounts for multiple sublocations)
  int32_t mutated_gene_index_start_1, mutated_gene_index_end_1;
  int8_t mutated_strand;
  gene.genomic_position_to_index_strand_1(start, mutated_gene_index_start_1, mutated_strand);
  gene.genomic_position_to_index_strand_1(end, mutated_gene_index_end_1, mutated_strand);
  int32_t gene_inactivating_overlap_length = floor(inactivate_overlap_fraction * static_cast<double>(gene.get_length()));
  
  // values of 0 mean that it was not within the gene
  if (   ((mutated_gene_index_start_1 != 0) && (mutated_gene_index_start_1 <= gene_inactivating_overlap_length))
      || ((mutated_gene_index_end_1 != 0) && (mutated_gene_index_end_1 <= gene_inactivating_overlap_length)) ) {
  
    if ((gene.type == "CDS") && (mut._type == SNP)) {
      is_inactivating = (snp_type == "nonsense");
    }
    else if ( ((mut._type == INS) || (mut._type == DEL) || (mut._type == SUB)) ) {
      int32_t size_change = mut.mutation_size_change(*this);
      if ((size_change <= 15) && (gene.type == "CDS")) {
        is_inactivating = (size_change!=3);
      } else {
        is_inactivating = true;
      }
    } else if (mut._type == MOB) {
      is_inactivating = true;
    }
  }
  
  return is_inactivating;
}
  
// Helper function
string cReferenceSequences::list_to_entry(const vector<string>& _list, const string& _ignore) {
  
  bool has_non_ignore = false;
  for(vector<string>::const_iterator it=_list.begin(); it!= _list.end(); it++) {
    if (*it != _ignore) {
      has_non_ignore = true;
      break;
    }
  }
  // Decide whether it has all ignore values or not
  
  if (!has_non_ignore) return "";
  return join(_list, multiple_separator);
}
  
  
void cReferenceSequences::annotate_1_mutation_in_genes(cDiffEntry& mut, vector<cFeatureLocation*>& within_gene_locs, uint32_t start, uint32_t end, bool ignore_pseudogenes)
{
  // Separated by semicolons (separate for each gene)
  vector<string> gene_name_list;
  vector<string> html_gene_name_list;
  vector<string> gene_product_list;
  vector<string> locus_tag_list;
  vector<string> gene_position_list;
  vector<string> gene_strand_list;
  vector<string> snp_type_list;
  vector<string> codon_position_is_indeterminate_list;
  vector<string> codon_position_list;
  vector<string> codon_number_list;
  vector<string> aa_position_list;
  vector<string> codon_ref_seq_list;
  vector<string> aa_ref_seq_list;
  vector<string> codon_new_seq_list;
  vector<string> aa_new_seq_list;
  vector<string> transl_table_list;
  // Separated by commas (put together for all genes)
  vector<string> genes_inactivated_list;
  vector<string> locus_tags_inactivated_list;
  vector<string> genes_overlapping_list;
  vector<string> locus_tags_overlapping_list;
  bool codon_position_is_indeterminate_in_at_least_one = false;
  
  for (vector<cFeatureLocation*>::iterator flit = within_gene_locs.begin(); flit != within_gene_locs.end(); flit++) {
    
    // Per gene values
    string gene_name;
    string html_gene_name;
    string gene_product;
    string locus_tag;
    string gene_position;
    string gene_strand;
    string snp_type;
    
    /// It can be within multiple genes, in which case we need to annotate
    /// the change it causes in each reading frame UGH! YUCKY!
    /// FOR NOW: just take the FIRST of the within genes...
    cFeatureLocation* gene_loc = *flit;
    cGeneFeature gene = (cGeneFeature)(*(gene_loc->get_feature()));
    
    gene_name = gene.name;
    
    html_gene_name = "<i>" + gene.name + "</i>";
    html_gene_name += (gene_loc->get_strand() == -1) ? "&nbsp;&larr;" : "&nbsp;&rarr;";
    
    gene_product = gene.product;
    locus_tag = gene.get_locus_tag();
    
    int32_t within_gene_start = gene_loc->is_top_strand() ? gene_loc->get_start_1() : gene_loc->get_end_1();
    
    if (start == end)
    {
      gene_position = to_string(abs(static_cast<int32_t>(start) - within_gene_start) + 1);
    }
    else
    {
      uint32_t gene_start = abs(static_cast<int32_t>(start) - within_gene_start) + 1;
      uint32_t gene_end = abs(static_cast<int32_t>(end) - within_gene_start) + 1;
      gene_position = (gene_start < gene_end)
      ? to_string(gene_start) + "-" + to_string(gene_end)  //hyphen
      : to_string(gene_end) + "-" + to_string(gene_start); //hyphen
    }

    string gene_nt_size = to_string(gene_loc->get_end_1() - gene_loc->get_start_1() + 1);
    string gene_strand_char = gene_loc->is_top_strand() ? ">" : "<";
    gene_strand = gene_strand_char;
    
    // Set deafaults
    string codon_position_is_indeterminate("0");
    string codon_position("NA");
    string codon_number("NA");
    string aa_position("NA");
    string codon_ref_seq("NA");
    string codon_new_seq("NA");
    string aa_ref_seq("NA");
    string aa_new_seq("NA");
    string transl_table("NA");
    
    // ...but the gene is a pseudogene or not a protein coding gene
    if (!ignore_pseudogenes && gene.pseudogene)
    {
      if ((mut._type == SNP) || (mut._type == RA)) {
        snp_type = "pseudogene";
      }
      gene_position = "pseudogene (" + gene_position + "/" + gene_nt_size + " nt)";
    }
    else if (gene.type != "CDS")
    {
      if ((mut._type == SNP) || (mut._type == RA)) {
        snp_type = "noncoding";
      }
      gene_position = "noncoding (" + gene_position + "/" + gene_nt_size + " nt)";
    }
    
    // only add gene information to SNPs and RA mutations that don't include indels
    else if ((mut._type != SNP) && !((mut._type == RA) && ((mut[MAJOR_BASE] != ".") && (mut[MINOR_BASE] != ".")))) {
      
      gene_position = "coding (" + gene_position + "/" + gene_nt_size + " nt)";
      
    } else {
      
      // These genes should be marked pseudo, so this should
      ASSERT(!(gene.start_is_indeterminate() && gene.end_is_indeterminate()), "Attempt to translate CDS with indeterminate start and end coordinates: " + gene["locus_tag"]);
      
      // Special additions for RA so that they can use the common code...
      if (mut._type == RA)  {
        string ra_seq_id = mut[SEQ_ID];
        int32_t ra_position = from_string<int32_t>(mut[POSITION]);
        mut["ref_seq"] = this->get_sequence_1(ra_seq_id, ra_position, ra_position);
        mut[NEW_SEQ] = ( (mut[MAJOR_BASE] == mut["ref_seq"]) ? mut[MINOR_BASE] : mut[MAJOR_BASE]);
      }
      
      // SNP and RA entries make it here
      
      // Get full gene nucleotide sequence. Pad with N's so that we can use a first or last
      // indeterminate codon if necessary
      
      // The position within a codon... indexed to start at 0.
      string gene_nt_sequence = gene.get_nucleotide_sequence((*this)[mut["seq_id"]]);
      
      // The position within a codon... indexed to start at 0.
      size_t indeterminate_codon_pos_offset_0 = 0;
      
      // Add padding to put us in-frame if we have an indeterminate start
      codon_position_is_indeterminate = "0";
      if (gene.start_is_indeterminate()) {
        indeterminate_codon_pos_offset_0 =  (3 - gene_nt_sequence.length() % 3) % 3;
        gene_nt_sequence = repeat_char('N', indeterminate_codon_pos_offset_0) + gene_nt_sequence;
        // flag so that we know the codon position is not to be trusted
        codon_position_is_indeterminate = "1";
        codon_position_is_indeterminate_in_at_least_one = true;
      }
      if (gene.end_is_indeterminate()) {
        gene_nt_sequence += repeat_char('N', gene_nt_sequence.length() % 3);
      }
      
      // The genomic position we are looking for
      int32_t mutated_genomic_position_1 = from_string<int32_t>(mut[POSITION]);
      
      // Translate to nucleotide index within the gene
      int32_t mutated_index_1(0);
      int8_t mutated_strand(0);
      gene.genomic_position_to_index_strand_1(mutated_genomic_position_1, mutated_index_1, mutated_strand);
      
      // Set the codon position that is mutated (including offset for indeterminate_start)
      int32_t mutated_codon_pos_1 = 1 + (mutated_index_1 + indeterminate_codon_pos_offset_0 - 1) % 3;
      
      int32_t mutated_codon_number_1 = 1 + int(floor(indeterminate_codon_pos_offset_0 + mutated_index_1 - 1)/3);
      codon_ref_seq = gene_nt_sequence.substr((mutated_codon_number_1-1)*3, 3);
      
      // Save what we know so far
      codon_position = to_string<int32_t>(mutated_codon_pos_1); // 1-indexed
      codon_number = to_string<int32_t>(mutated_codon_number_1);
      aa_position = to_string<int32_t>(mutated_codon_number_1);
      
      aa_ref_seq = translate_codon(codon_ref_seq, gene.translation_table, ( gene.start_is_indeterminate() && (mutated_codon_number_1 == 1) ) ? 2 : mutated_codon_number_1, gene.get_locus_tag());
      
      // Generate mutated sequence
      codon_new_seq = codon_ref_seq;
      //#remember to revcom the change if gene is on opposite strand
      codon_new_seq[mutated_codon_pos_1 - 1] = (mutated_strand == 1) ? mut[NEW_SEQ][0] : reverse_complement(mut[NEW_SEQ])[0];
      aa_new_seq = translate_codon(codon_new_seq, gene.translation_table, ( gene.start_is_indeterminate() && (mutated_codon_number_1 == 1) ) ? 2 : mutated_codon_number_1, gene.get_locus_tag());
      transl_table = to_string(gene.translation_table);
      
      if ((aa_ref_seq != "*") && (aa_new_seq == "*"))
        snp_type = "nonsense";
      else if (aa_ref_seq != aa_new_seq)
        snp_type = "nonsynonymous";
      else
        snp_type = "synonymous";
      
    }
    
    codon_position_is_indeterminate_list.push_back(codon_position_is_indeterminate);
    codon_position_list.push_back(codon_position);
    codon_number_list.push_back(codon_number);
    aa_position_list.push_back(aa_position);
    codon_ref_seq_list.push_back(codon_ref_seq);
    codon_new_seq_list.push_back(codon_new_seq);
    aa_ref_seq_list.push_back(aa_ref_seq);
    aa_new_seq_list.push_back(aa_new_seq);
    transl_table_list.push_back(transl_table);
    
    // --------------  Begin "genes_inactivated" field
    if (mut.is_mutation()) {
      
      // Can only be one of inactivating and overlapping
      if (mutation_overlapping_gene_is_inactivating(mut, snp_type, start, end, gene, k_inactivate_overlap_fraction)) {
        genes_inactivated_list.push_back(gene.name);
        locus_tags_inactivated_list.push_back(gene.get_locus_tag());
      } else {
        genes_overlapping_list.push_back(gene.name);
        locus_tags_overlapping_list.push_back(gene.get_locus_tag());
      }
      
    }
    // --------------  End "genes_inactivated" field
    
    // Push everything on the list
    gene_name_list.push_back(gene_name);
    html_gene_name_list.push_back(html_gene_name);
    gene_product_list.push_back(gene_product);
    locus_tag_list.push_back(locus_tag);
    gene_position_list.push_back(gene_position);
    gene_strand_list.push_back(gene_strand);
    snp_type_list.push_back(snp_type);
  }
  
  // These are always present for every gene
  mut["gene_name"] = join(gene_name_list, multiple_separator);
  mut["html_gene_name"] = join(html_gene_name_list, html_multiple_separator);
  mut["gene_product"] = join(gene_product_list, multiple_separator);
  mut["locus_tag"] = join(locus_tag_list, multiple_separator);
  mut["gene_position"] = join(gene_position_list, multiple_separator);
  mut["gene_strand"] = join(gene_strand_list, multiple_separator);
  mut["snp_type"] = join(snp_type_list, multiple_separator);
  
  // These may not be present
  mut["codon_position_is_indeterminate"] = list_to_entry(codon_position_is_indeterminate_list, "0");
  mut["codon_position"] = list_to_entry(codon_position_list, "NA");
  mut["codon_number"] = list_to_entry(codon_number_list, "NA");
  mut["aa_position"] = list_to_entry(aa_position_list, "NA");
  mut["codon_ref_seq"] = list_to_entry(codon_ref_seq_list, "NA");
  mut["codon_new_seq"] = list_to_entry(codon_new_seq_list, "NA");
  mut["aa_ref_seq"] = list_to_entry(aa_ref_seq_list, "NA");
  mut["aa_new_seq"] = list_to_entry(aa_new_seq_list, "NA");
  mut["transl_table"] = list_to_entry(transl_table_list, "NA");
  
  if (genes_inactivated_list.size() > 0)
    mut["genes_inactivated"] = join(genes_inactivated_list, ",");
  if (locus_tags_inactivated_list.size() > 0)
    mut["locus_tags_inactivated"] = join(locus_tags_inactivated_list, ",");
  if (genes_overlapping_list.size() > 0)
    mut["genes_overlapping"] = join(genes_overlapping_list, ",");
  if (locus_tags_overlapping_list.size() > 0)
    mut["locus_tags_overlapping"] = join(locus_tags_overlapping_list, ",");
}
  
void cReferenceSequences::annotate_1_mutation(cDiffEntry& mut, uint32_t start, uint32_t end, bool repeat_override, bool ignore_pseudogenes)
{
  
  // Initialize everything, even though we don't always use it.
  // This is important if our input is a GenomeDiff that already
  // has some of this information in it.
  mut["locus_tag"] = "";
  mut["aa_position"] = "";
  mut["aa_ref_seq"] = "";
  mut["aa_new_seq"] = "";
  mut["codon_position"] = "";
  mut["codon_ref_seq"] = "";
  mut["codon_new_seq"] = "";
  mut["gene_name"] = "";
  mut["html_gene_name"] = "";
  mut["gene_position"] = "";
  mut["gene_product"] = "";
  mut["_gene_product_hide"] = "";
  
  
  mut["genes_overlapping"] = "";
  mut["locus_tags_overlapping"] = "";

  // Overlaps ANY part of these genes
  
  mut["genes_inactivated"] = "";
  mut["locus_tags_inactivated"] = "";

  // This is a SUBSET of genes_overlapping
  // Must overlap just one gene
  // For mutations within first 80% of a gene's length, includes:
  //  SNP if it is a nonsense mutation AND gene is protein coding.
  //  INS/DEL/SUB if it is inframe AND the size change is <=15 nt AND gene is protein coding.
  //  INS/DEL/SUB if the size change is >15 nt for all genes
  //  MOB for all genes
  
  mut["genes_promoter"] = "";
  mut["locus_tags_promoter"] = "";

  // Does NOT overlap gene and occurs within an intergenic region
  // within a region extending upstream of the start codon by k_promoter_distance
  // If two genes qualify due to divergent transcription (<-- -->), the closer one is chosen.
  
  
  string seq_id = mut["seq_id"];

  //or die "Unknown seq_id in reference sequence info: $seq_id\n";

  cFeatureLocationList& gene_list_ref = (*this)[seq_id].m_gene_locations;
  cFeatureLocationList& repeat_list_ref = (*this)[seq_id].m_repeat_locations;

  int32_t size = end - start + 1;

  cFeatureLocation* prev_gene_loc(NULL);
  cFeatureLocation* next_gene_loc(NULL);
  vector<cFeatureLocation*> within_gene_locs;
  vector<cFeatureLocation*> between_gene_locs;
  vector<cFeatureLocation*> inside_left_gene_locs;
  vector<cFeatureLocation*> inside_right_gene_locs;

  cFeatureLocation* repeat_ptr(NULL);
  if (repeat_override)
  {
    assert(start == end);
    repeat_ptr = get_overlapping_feature(repeat_list_ref, start);
    if (repeat_ptr != NULL)
    {
      within_gene_locs.push_back(repeat_ptr);
    }
  }

  if (repeat_ptr == NULL)
    find_nearby_genes(gene_list_ref, start, end, within_gene_locs, between_gene_locs, inside_left_gene_locs, inside_right_gene_locs, prev_gene_loc, next_gene_loc);

  // Mutation is intergenic
  if (within_gene_locs.size() + between_gene_locs.size() + inside_left_gene_locs.size() + inside_right_gene_locs.size() == 0)
  {
    if ((mut._type == SNP) || (mut._type == RA)) {
      mut["snp_type"] = "intergenic";
    }
    
    cGeneFeature prev_gene, next_gene;
    if (prev_gene_loc) prev_gene = (cGeneFeature)*(prev_gene_loc->get_feature());
    if (next_gene_loc) next_gene = (cGeneFeature)*(next_gene_loc->get_feature());
    
    // --------------  Begin "genes_promoter" field

    
    /* code for assigning only to closest one
    if (mut.is_mutation()) {
      // Decide on promoter mutations
      int32_t prev_gene_distance(numeric_limits<int32_t>::max()), next_gene_distance(numeric_limits<int32_t>::max());
      if (prev_gene.name.size() > 0) {
        int32_t start_coord = mut.get_reference_coordinate_start().get_position();
        for (cFeatureLocationList::iterator it = prev_gene.m_locations.begin(); it != prev_gene.m_locations.end(); it++) {
          if (it->get_strand() == -1) {
            int32_t dist = start - it->get_end_1();
            prev_gene_distance = min(dist, prev_gene_distance);
          }
        }
      }
      if (next_gene.name.size() > 0) {
        int32_t start_coord = mut.get_reference_coordinate_start().get_position();
        for (cFeatureLocationList::iterator it = prev_gene.m_locations.begin(); it != prev_gene.m_locations.end(); it++) {
          if (it->get_strand() == +1) {
            int32_t dist = it->get_start_1() - end;
            next_gene_distance = min(dist, next_gene_distance);
          }
        }
      }
      
      
      // ties go to prev_gene!
      if ((prev_gene_distance <= k_promoter_distance) && (prev_gene_distance <= next_gene_distance)) {
        mut["genes_promoter"] = prev_gene.name;
        mut["locus_tags_promoter"] = prev_gene.get_locus_tag();

      } else if (next_gene_distance <= k_promoter_distance) {
        mut["genes_promoter"] = next_gene.name;
        mut["locus_tags_promoter"] = next_gene.get_locus_tag();

      }
    }
    */
    
    // Code for assigning to multiple promoters
    if (mut.is_mutation()) {
      int32_t prev_gene_distance(numeric_limits<int32_t>::max()), next_gene_distance(numeric_limits<int32_t>::max());
      if (prev_gene.name.size() > 0) {
        int32_t start_coord = mut.get_reference_coordinate_start().get_position();
        for (cFeatureLocationList::iterator it = prev_gene.m_locations.begin(); it != prev_gene.m_locations.end(); it++) {
          if (it->get_strand() == -1) {
            int32_t dist = start - it->get_end_1();
            prev_gene_distance = min(dist, prev_gene_distance);
          }
        }
      }
      if (next_gene.name.size() > 0) {
        int32_t start_coord = mut.get_reference_coordinate_start().get_position();
        for (cFeatureLocationList::iterator it = prev_gene.m_locations.begin(); it != prev_gene.m_locations.end(); it++) {
          if (it->get_strand() == +1) {
            int32_t dist = it->get_start_1() - end;
            next_gene_distance = min(dist, next_gene_distance);
          }
        }
      }
      
      vector<string> genes_promoter_list;
      vector<string> locus_tags_promoter_list;

      if ((prev_gene_distance <= k_promoter_distance)) {
        
        genes_promoter_list.push_back(prev_gene.name);
        locus_tags_promoter_list.push_back(prev_gene.get_locus_tag());
        
      } else if (next_gene_distance <= k_promoter_distance) {
        
        genes_promoter_list.push_back(next_gene.name);
        locus_tags_promoter_list.push_back(next_gene.get_locus_tag());
        
      }
      
      mut["genes_promoter"] = join(genes_promoter_list, ",");
      mut["locus_tags_promoter"] = join(locus_tags_promoter_list, ",");

    }
    
    
    // --------------  End "genes_promoter" field

    
    mut["gene_name"] += (prev_gene.name.size() > 0) ? prev_gene.name : no_gene_name;
    mut["gene_name"] += intergenic_separator;
    mut["gene_name"] += (next_gene.name.size() > 0) ? next_gene.name : no_gene_name;

    mut["html_gene_name"] += (prev_gene.name.size() > 0) ? "<i>" + prev_gene.name + "</i>" : no_gene_name;
    if (prev_gene.name.size() > 0) mut["html_gene_name"] += (prev_gene_loc->get_strand() == -1) ? "&nbsp;&larr;" : "&nbsp;&rarr;";
    mut["html_gene_name"] += "&nbsp;";
    mut["html_gene_name"] += html_intergenic_separator;
    if (next_gene.name.size() > 0) mut["html_gene_name"] += (next_gene_loc->get_strand() == -1) ? "&nbsp;&larr;" : "&nbsp;&rarr;";
    mut["html_gene_name"] += "&nbsp;";
    mut["html_gene_name"] += (next_gene.name.size() > 0) ? "<i>" + next_gene.name + "</i>": no_gene_name;
    
    mut["locus_tag"] += (prev_gene.get_locus_tag().size() > 0) ? prev_gene.get_locus_tag() : no_gene_name;
    mut["locus_tag"] += intergenic_separator;
    mut["locus_tag"] += (next_gene.get_locus_tag().size() > 0) ? next_gene.get_locus_tag() : no_gene_name;

    if (prev_gene.name.size() > 0)
    {
      mut["gene_position"] += "intergenic (";
      mut["gene_position"] += prev_gene_loc->is_top_strand() ? "+" : "-"; //hyphen
      mut["gene_position"] += to_string(start - prev_gene_loc->get_end_1());
    }
    else
    {
      mut["gene_position"] += "intergenic (–";
    }
    mut["gene_position"] += intergenic_separator;
    if (next_gene.name.size() > 0)
    {
      mut["gene_position"] += next_gene_loc->is_top_strand() ? "-" : "+"; //hyphen
      mut["gene_position"] += to_string(next_gene_loc->get_start_1() - end);
    }
    else
    {
      mut["gene_position"] += "–"; //en-dash
    }
    mut["gene_position"] += ")";

    mut["gene_product"] += (prev_gene.product.size() > 0) ? prev_gene.product : "–"; //en-dash
    mut["gene_product"] += intergenic_separator;
    mut["gene_product"] += (next_gene.product.size() > 0) ? next_gene.product : "–"; //en-dash

    return;
  }
  // Mutation is completely within one or more genes
  else if (within_gene_locs.size() > 0)
  {
    annotate_1_mutation_in_genes(mut, within_gene_locs, start, end, ignore_pseudogenes);
  }

  //The mutation actually contains several genes or overlaps the ends of the gene
  else if (between_gene_locs.size() + inside_left_gene_locs.size() + inside_right_gene_locs.size() > 0)
  {

    vector<string> gene_name_list, locus_tag_list; // human readable, indicates partial overlaps with brackets []

    for (vector<cFeatureLocation*>::iterator it=inside_left_gene_locs.begin(); it != inside_left_gene_locs.end(); it++)
    {
      cGeneFeature gene = (cGeneFeature)(*((*it)->get_feature()));

      gene_name_list.push_back("[" + gene.name + "]");

      if (gene.get_locus_tag().size()) {
        locus_tag_list.push_back("[" + gene.get_locus_tag() + "]");
      }

    }
    for (vector<cFeatureLocation*>::iterator it=between_gene_locs.begin(); it != between_gene_locs.end(); it++)
    {
      cGeneFeature gene = (cGeneFeature)(*((*it)->get_feature()));
      
      gene_name_list.push_back(gene.name);

      if (gene.get_locus_tag().size()) {
        locus_tag_list.push_back("[" + gene.get_locus_tag() + "]");
      }

    }
    for (vector<cFeatureLocation*>::iterator it=inside_right_gene_locs.begin(); it != inside_right_gene_locs.end(); it++)
    {
      cGeneFeature gene = (cGeneFeature)(*((*it)->get_feature()));

      gene_name_list.push_back("[" + gene.name + "]");

      if (gene.get_locus_tag().size()) {
        locus_tag_list.push_back("[" + gene.get_locus_tag() + "]");
      }

    }
    
    // --------------  Begin "genes_inactivated" and other genes fields
    vector<string> genes_overlapping_list, locus_tags_overlapping_list; // straight list of genes

    if (mut.is_mutation()) {
    
      vector<string> inactivated_gene_list, inactivated_locus_tag_list;

      for (vector<cFeatureLocation*>::iterator it = inside_left_gene_locs.begin(); it != inside_left_gene_locs.end(); it++) {
        
        cGeneFeature gene = (cGeneFeature)(*((*it)->get_feature()));
        
        if (mutation_overlapping_gene_is_inactivating(mut, "", start, end, gene, k_inactivate_overlap_fraction)) {
          inactivated_gene_list.push_back(gene.name);
          inactivated_locus_tag_list.push_back(gene.get_locus_tag());
        } else {
          genes_overlapping_list.push_back(gene.name);
          locus_tags_overlapping_list.push_back(gene.get_locus_tag());
        }
        
      }
      
      // Note difference for within is that we know these are inactivating
      for (vector<cFeatureLocation*>::iterator it = between_gene_locs.begin(); it != between_gene_locs.end(); it++) {
        
        cGeneFeature gene = (cGeneFeature)(*((*it)->get_feature()));
        
        inactivated_gene_list.push_back(gene.name);
        inactivated_locus_tag_list.push_back(gene.get_locus_tag());
        
      }
      
      for (vector<cFeatureLocation*>::iterator it = inside_right_gene_locs.begin(); it != inside_right_gene_locs.end(); it++) {
        
        cGeneFeature gene = (cGeneFeature)(*((*it)->get_feature()));
        
        if (mutation_overlapping_gene_is_inactivating(mut, "", start, end, gene, k_inactivate_overlap_fraction)) {
          inactivated_gene_list.push_back(gene.name);
          inactivated_locus_tag_list.push_back(gene.get_locus_tag());
        } else {
          genes_overlapping_list.push_back(gene.name);
          locus_tags_overlapping_list.push_back(gene.get_locus_tag());
        }
        
      }
      
      mut["genes_inactivated"] = join(inactivated_gene_list, ",");
      mut["locus_tags_inactivated"] = join(inactivated_locus_tag_list, ",");
      
      mut["genes_overlapping"] = join(genes_overlapping_list, ",");
      mut["locus_tags_overlapping"] = join(locus_tags_overlapping_list, ",");
      
    }
    // --------------  End "genes_inactivated" field
    
    // We ended up calling this function a lot.
    string sJoinedGeneList = join(gene_name_list, ", ");
    
    mut["gene_product"] += sJoinedGeneList;
    mut["_gene_product_hide"] = "<i>" + mut["gene_product"] + "</i>";
    
    // @MDS0004 - This number here will condense the number of genes listed.
    // Change to show how many appear in HTML. This is a javascript call.
    //
    // Non-Button Toggle - Replace <input>
    // <a href=\"javascript:hideTog('" + sID + "');\">Toggle Genes</a>
    if(gene_name_list.size() > 15)
    {
      string sID = "gene_hide_" + to_string(mut._type) + "_" + mut._id;
      string sButtonID = sID + "_button";
      mut["_gene_product_hide"] =
        "<i title=\"" + sJoinedGeneList + "\"><b>" +
        to_string(gene_name_list.size()) +
        " genes</b> <noscript>Javascript Disabled: All genes shown.<br></noscript></i>"
        + "<noscript>" + mut["gene_product"] + "</noscript>"
        + "<div id=\"" + sID + "\" class=\"hidden\">"
        + mut["gene_product"] + "</div>"
        + "<input id=\"" + sButtonID + "\" type=\"button\" onclick=\"hideTog('" + sID + "');showTog('" + sButtonID + "')\" value=\"Show\" />";
      mut["gene_product"] = "<i>";
      mut["gene_product"] += "<b>" + to_string(gene_name_list.size()) + " genes</b><BR>";
      mut["gene_product"] += sJoinedGeneList;
      mut["gene_product"] += "</i>";
    }

    if (gene_name_list.size() == 1) {
      mut["gene_name"] = gene_name_list[0];
      mut["html_gene_name"] = "<i>" + gene_name_list[0] + "</i>";
    } else {
      mut["gene_name"] = gene_name_list.front() + "–" + gene_name_list.back();  //en-dash  
      mut["html_gene_name"] = "<i>" + gene_name_list.front() + "</i>–<i>" + gene_name_list.back() + "</i>";  //en-dash      
    }

    if (locus_tag_list.size() == 1) {
      mut["locus_tag"] = locus_tag_list[0];
    }
    else if (locus_tag_list.size() > 1) {
      mut["locus_tag"] = locus_tag_list.front() + "–" + locus_tag_list.back();  //en-dash
    }
  }
}
  
// Adds "mutation_category" to GD entry
void cReferenceSequences::categorize_1_mutation(cDiffEntry& mut, int32_t large_size_cutoff)
{
  if (mut._type == SNP) {
    
    ASSERT(mut.entry_exists("snp_type"), "Attempt to classify SNP before annotating its snp type");
    mut["mutation_category"] = "snp_" + mut["snp_type"];
    
  } else if (mut._type == DEL) {
    
    if (from_string<int32_t>(mut["size"]) > large_size_cutoff) {
      mut["mutation_category"] = "large_deletion";
    } else {
      mut["mutation_category"] = "small_indel";
    }
    
  } else if (mut._type == INS) {
    int32_t ins_size = mut[NEW_SEQ].size();
    
    if (ins_size > large_size_cutoff) {
      mut["mutation_category"] = "large_insertion";
    } else {
      mut["mutation_category"] = "small_indel";
    }
    
  } else if (mut._type == SUB) {
    int32_t old_size = from_string<int32_t>(mut[SIZE]);
    int32_t new_size = mut[NEW_SEQ].size();
    
    if (abs(new_size - old_size) > large_size_cutoff) {
      mut["mutation_category"] ="large_substitution";
    } else {
      mut["mutation_category"] = "small_indel";
    }
  } else if (mut._type == CON) {
    
    mut["mutation_category"] = "gene_conversion";
    
  } else if (mut._type == MOB) {
    
    mut["mutation_category"] = "mobile_element_insertion";
    
  } else if (mut._type == AMP) {
    int32_t this_size = from_string<uint32_t>(mut[SIZE]) * (from_string<uint32_t>(mut["new_copy_number"]) - 1);
    
    if (this_size > large_size_cutoff) {
      mut["mutation_category"] = "large_amplification";
    } else {
      mut["mutation_category"] = "small_indel";
    }
    
  } else if (mut._type == INV) {
    
    mut["mutation_category"] = "inversion";
    
  } else {
    ERROR("Could not classify mutation:\n" + mut.as_string());
  }

}

void cReferenceSequences::annotate_mutations(cGenomeDiff& gd, bool only_muts, bool ignore_pseudogenes, bool compare_mode, int32_t large_size_cutoff, bool verbose)
{
  //keep track of other mutations that affect SNPs
  //because we may double-hit a codon
  //=>must be sure that these are from the same input file...so special flag for compare mode!

  // The proper way to do this is to create list of SNPs that have been hit
  // hashed by gene protein accession ID and AA position within gene
  // and have the annotation point to them (and back at them)
  // so that the codon will be correctly updated with all changes and we can notify the
  // changes that their SNP_type is not really SNP, but multiple hit SNP.
  
  // Be sure that we are sorted.
  gd.sort();
  
  diff_entry_list_t muts = gd.show_list();
  
  string default_key = "default";
  vector<string>compare_key_list;
  if (compare_mode && muts.size()) {
    diff_entry_ptr_t p = *muts.begin();
    for(cDiffEntry::iterator it = p->begin(); it != p->end(); ++it) {
      if (it->first.compare(0, 10, "frequency_") == 0) {
        compare_key_list.push_back(it->first);
      }
    }
  }
  if (compare_key_list.size() == 0) compare_key_list.push_back(default_key);
  
  map<string, vector<cDiffEntry*> > snp_muts;

  for (diff_entry_list_t::iterator it=muts.begin(); it!=muts.end(); it++)
  {
    cDiffEntry& mut= **it;
    if (verbose) cerr << "Annotating: " << mut << endl;

    if (only_muts && !(mut.is_mutation())) continue;
    
    switch (mut._type)
    {
      case SNP:{
        mut["_ref_seq"] = get_sequence_1(mut["seq_id"], from_string<uint32_t>(mut["position"]), from_string<int32_t>(mut["position"]));
        annotate_1_mutation(mut, mut.get_reference_coordinate_start().get_position(), mut.get_reference_coordinate_end().get_position(), false, ignore_pseudogenes);
      } break;
        
      case SUB:
      case DEL:
      case INS:
      case CON:
      case MOB:
      case AMP:{
        annotate_1_mutation(mut, mut.get_reference_coordinate_start().get_position(), mut.get_reference_coordinate_end().get_position());
      } break;
        
      case INV:{
        annotate_1_mutation(mut, from_string<int32_t>(mut["position"]), from_string<int32_t>(mut["position"]));
        mut["gene_name_1"] = mut["gene_name"];
        mut["gene_product_1"] = mut["gene_product"];
        annotate_1_mutation(mut, from_string<int32_t>(mut["position"]) + from_string<int32_t>(mut["size"])-1, from_string<int32_t>(mut["position"]) + from_string<int32_t>(mut["size"])-1);
        mut["gene_name_2"] = mut["gene_name"];
        mut["gene_product_2"] = mut["gene_product"];
        mut.erase("gene_name");
        mut.erase("gene_product");
      } break;
        
      case JC:{
        cDiffEntry side_1;
        side_1[SEQ_ID] = mut["side_1_seq_id"];
        annotate_1_mutation(side_1, from_string<int32_t>(mut["side_1_position"]), from_string<int32_t>(mut["side_1_position"]), true);
        //copy over entries with prefix
        for(diff_entry_map_t::iterator it=side_1.begin(); it!=side_1.end(); it++)
        {
          mut["_side_1"+ it->first] = it->second;
        }
        
        cDiffEntry side_2;
        side_2[SEQ_ID] = mut["side_2_seq_id"];
        annotate_1_mutation(side_2, from_string<int32_t>(mut["side_2_position"]), from_string<int32_t>(mut["side_2_position"]), true);
        //copy over entries with prefix
        for(diff_entry_map_t::iterator it=side_2.begin(); it!=side_2.end(); it++)
        {
          mut["_side_2"+ it->first] = it->second;
        }
      } break;
        
      case RA:{
        annotate_1_mutation(mut, from_string<int32_t>(mut["position"]), from_string<int32_t>(mut["position"]));
      } break;
        
      case MC:{
        annotate_1_mutation(mut, from_string<int32_t>(mut["start"]), from_string<int32_t>(mut["end"]));
      } break;
        
      case CN:{
        annotate_1_mutation(mut, from_string<int32_t>(mut["start"]), from_string<int32_t>(mut["end"]));
      } break;
        
      default:{
      } break;
    }
    
    // Perform mutation classification
    if (mut.is_mutation()) {
      categorize_1_mutation(mut, large_size_cutoff);
    }
    
    // Track codon hits for multiple SNPs (or RA) in same codon.
    if ( ( mut._type == SNP || mut._type == RA ) && mut.entry_exists("codon_number") && mut.entry_exists("codon_position") ) {
      for(vector<string>::iterator itc = compare_key_list.begin(); itc != compare_key_list.end(); ++itc) {
        if (*itc == default_key) {
          snp_muts[default_key].push_back(&mut);
        } else {
          if ( from_string<double>(mut[*itc]) != 0)
            snp_muts[*itc].push_back(&mut);
        }
      }
    }

  }
  
  // Hugely inefficient step --!!
  // Scan SNPs to see if they affect the same codon, merge changes, and 
  // update amino acid changes assumes the list of snps is sorted.
  for(vector<string>::iterator it_key = compare_key_list.begin(); it_key != compare_key_list.end(); it_key++) {
    string on_key = *it_key;
    for (size_t i = 0; i < snp_muts[on_key].size(); i++){
      
      string i_seq_id = (*snp_muts[on_key][i])["seq_id"];
      int32_t i_position = from_string<int32_t>((*snp_muts[on_key][i])["position"]);
      
      vector<string> codon_number_list_i = split((*snp_muts[on_key][i])["codon_number"], multiple_separator);
      vector<string> codon_position_list_i = split((*snp_muts[on_key][i])["codon_position"], multiple_separator);

      for (size_t ii = 0; ii < codon_number_list_i.size(); ii++) {
      
        if ((codon_number_list_i[ii] == "") || (codon_position_list_i[ii] == ""))
          continue;
        int32_t i_codon_number = from_string<int32_t>(codon_number_list_i[ii]);
        
        for (size_t j = i + 1; j < snp_muts[on_key].size(); j++) {
          
          
          string j_seq_id = (*snp_muts[on_key][j])["seq_id"];
          int32_t j_position = from_string<int32_t>((*snp_muts[on_key][j])["position"]);
          
          if (i_seq_id != j_seq_id)
            continue;
          
          // This skips cases of the same SNP and RA
          if (i_position == j_position)
            continue;
          
          vector<string> codon_number_list_j = split((*snp_muts[on_key][j])["codon_number"], multiple_separator);
          vector<string> codon_position_list_j = split((*snp_muts[on_key][j])["codon_position"], multiple_separator);
          
          for (size_t jj = 0; jj < codon_number_list_j.size(); jj++) {

            
            if ((codon_number_list_j[jj] == "") || (codon_position_list_j[jj] == ""))
              continue;
            int32_t j_codon_number = from_string<int32_t>(codon_number_list_j[jj]);
            
            if (i_codon_number != j_codon_number)
              continue;
            
            
            int32_t i_codon_position = from_string<int32_t>(codon_position_list_i[ii]);
            int32_t j_codon_position = from_string<int32_t>(codon_position_list_j[jj]);
            
            if ((*this)[i_seq_id].get_circular_distance_1(i_position, j_position) != abs(i_codon_position-j_codon_position))
              continue;
            
            // Don't do this for SNPs that are not 100% frequency
            if (   ((*snp_muts[on_key][i]).entry_exists(FREQUENCY) && ( from_string<double>((*snp_muts[on_key][i])[FREQUENCY]) != 1.0 ))
                || ((*snp_muts[on_key][j]).entry_exists(FREQUENCY) && ( from_string<double>((*snp_muts[on_key][j])[FREQUENCY]) != 1.0 )) )
            {
              
              
              // Create or fill the field marking multiple SNPs in the same codon
              if (!(*snp_muts[on_key][i]).entry_exists("multiple_polymorphic_SNPs_in_same_codon")) {
                vector<string> empty(codon_number_list_i.size(), "0");
                (*snp_muts[on_key][i])["multiple_polymorphic_SNPs_in_same_codon"] = join(empty, multiple_separator);
              } else {
                // split
                vector<string> current = split((*snp_muts[on_key][i])["multiple_polymorphic_SNPs_in_same_codon"], multiple_separator);
                current[ii] = "1";
                (*snp_muts[on_key][i])["multiple_polymorphic_SNPs_in_same_codon"] = join(current, multiple_separator);
              }
              
              if (!(*snp_muts[on_key][j]).entry_exists("multiple_polymorphic_SNPs_in_same_codon")) {
                vector<string> empty(codon_number_list_j.size(), "0");
                (*snp_muts[on_key][j])["multiple_polymorphic_SNPs_in_same_codon"] = join(empty, multiple_separator);
              } else {
                // split
                vector<string> current = split((*snp_muts[on_key][j])["multiple_polymorphic_SNPs_in_same_codon"], multiple_separator);
                current[jj] = "1";
                (*snp_muts[on_key][j])["multiple_polymorphic_SNPs_in_same_codon"] = join(current, multiple_separator);
              }
              continue;
            }
            
            vector<string> codon_new_seq_list_i = split((*snp_muts[on_key][i])["codon_new_seq"], multiple_separator);
            string new_codon = codon_new_seq_list_i[ii];
            char new_char = (*snp_muts[on_key][j])["new_seq"][0];
            if ((*snp_muts[on_key][i])["gene_strand"] == "<") new_char = reverse_complement(new_char);
            new_codon[j_codon_position - 1] = new_char;
            
            vector<string> codon_new_seq_list_j = split((*snp_muts[on_key][j])["codon_new_seq"], multiple_separator);
            vector<string> aa_new_seq_list_i = split((*snp_muts[on_key][i])["aa_new_seq"], multiple_separator);
            vector<string> aa_new_seq_list_j = split((*snp_muts[on_key][j])["aa_new_seq"], multiple_separator);

            codon_new_seq_list_i[ii] = new_codon;
            codon_new_seq_list_j[jj] = new_codon;
            aa_new_seq_list_i[ii] =  translate_codon(new_codon, from_string((*snp_muts[on_key][i])["transl_table"]), from_string((*snp_muts[on_key][i])["aa_position"]));
            aa_new_seq_list_j[jj] =  translate_codon(new_codon, from_string((*snp_muts[on_key][j])["transl_table"]), from_string((*snp_muts[on_key][j])["aa_position"]));
            
            (*snp_muts[on_key][i])["codon_new_seq"] = join(codon_new_seq_list_i, multiple_separator);
            (*snp_muts[on_key][j])["codon_new_seq"] = join(codon_new_seq_list_j, multiple_separator);
            (*snp_muts[on_key][i])["aa_new_seq"] = join(aa_new_seq_list_i, multiple_separator);
            (*snp_muts[on_key][j])["aa_new_seq"] = join(aa_new_seq_list_j, multiple_separator);
            
            // @JEB: Debatable what we should do with SNP-type here. For now it remains that of the single mutations...
          }
        }
      }
    } // SNP handling
  }//for
}

void cReferenceSequences::polymorphism_statistics(Settings& settings, Summary& summary)
{
  string reference_fasta_file_name = settings.reference_fasta_file_name;
  vector<string> seq_ids = this->seq_ids();

  // some local variable lookups for convenience
  double log10_ref_length = log(this->total_length()) / log(10);

  string count_file_name = settings.error_counts_file_name;

  ifstream COUNT(count_file_name.c_str());
  assert(COUNT.is_open());
  string count_header_line, count_covariate_line;
  getline(COUNT, count_covariate_line); // ignore the first line

  // we parse the covariates to know how many different qualities we are dealing with...
  covariates_used_t         covariate_used;
  covariates_max_t          covariate_max;
  covariates_enforce_max_t  covariate_enforce_max;
  covariates_offset_t       covariate_offset;
  bool                      per_position;
  cErrorTable::read_covariates(count_covariate_line, covariate_used, covariate_max, covariate_enforce_max, covariate_offset, per_position);

  getline(COUNT, count_header_line); // second line is the header

  vector<string> count_header_list = split(count_header_line, "\t");

  uint32_t count_column = UNDEFINED_UINT32;
  uint32_t quality_column = UNDEFINED_UINT32;
  for (uint32_t i = 0; i < count_header_list.size(); i++)
  {
    if (count_header_list[i] == "quality")
      quality_column = i;
    else if (count_header_list[i] == "count")
      count_column = i;
  }

  ASSERT( (quality_column != UNDEFINED_UINT32) && (count_column != UNDEFINED_UINT32),
          "'quality' and 'count' columns not found in file: " + count_file_name);

  vector<uint32_t> quality_count_list(covariate_max[k_quality]);
  string line;
  while (COUNT.good())
  {
    getline(COUNT, line);
    vector<string> line_list = split(line,  "\t");
    if (line_list.size() < 2) break; // empty line
    uint32_t count = from_string<uint32_t>(line_list[count_column]);
    uint32_t quality = from_string<uint32_t>(line_list[quality_column]);
    quality_count_list[quality] += count;
  }
  COUNT.close();

  string genome_error_counts_file_name = settings.genome_error_counts_file_name;

  ofstream GEC(genome_error_counts_file_name.c_str());
  assert(GEC.is_open());
  for (uint32_t i = 1; i < quality_count_list.size(); i++)
  {
    uint32_t val = 0;
    val = quality_count_list[i];
    GEC << val << endl;
  }
  GEC.close();

  string polymorphism_statistics_input_file_name = settings.polymorphism_statistics_input_file_name;
  string polymorphism_statistics_output_file_name = settings.polymorphism_statistics_output_file_name;

  /// Load the older GenomeDiff and add new fields
  string ra_mc_genome_diff_file_name = settings.ra_mc_genome_diff_file_name;
  cGenomeDiff gd(ra_mc_genome_diff_file_name);

  string polymorphism_statistics_r_script_file_name = settings.polymorphism_statistics_r_script_file_name;
  string polymorphism_statistics_r_script_log_file_name = settings.polymorphism_statistics_r_script_log_file_name;
  uint64_t total_reference_length = summary.sequence_conversion.total_reference_sequence_length;

  string command = "R --vanilla total_length=" + to_string<uint32_t>(total_reference_length) +
    " in_file="   + cString(polymorphism_statistics_input_file_name).escape_shell_chars() +
    " out_file="  + cString(polymorphism_statistics_output_file_name).escape_shell_chars() +
    " qual_file=" + cString(genome_error_counts_file_name).escape_shell_chars() +
    " < "         + cString(polymorphism_statistics_r_script_file_name).escape_shell_chars() +
    " > "         + cString(polymorphism_statistics_r_script_log_file_name).escape_shell_chars();
  SYSTEM(command, false, false, false); //NOTE: Not escaping shell characters here.

  // Read R file and add new results corresponding to all columns
  ifstream ROUT(polymorphism_statistics_output_file_name.c_str());
  assert(ROUT.is_open()); // or die "Could not find file: $polymorphism_statistics_output_file_name";
  string header;
  getline(ROUT, header);
  vector<string> header_list = split(header, "\t");

  cGenomeDiff new_gd;

  diff_entry_list_t muts = gd.evidence_list();
  for (diff_entry_list_t::iterator it=muts.begin(); it!=muts.end(); it++)
  {
    cDiffEntry& mut= **it;

    // No lines exist for user_defined RA evidence = 1 (if =2 then we need to erase any reject reasons)
    if (mut.entry_exists("user_defined_no_poly"))
    {
      mut.erase("user_defined_no_poly");
      new_gd.add(mut);
      continue;
    }
    
    // lines only exist for RA evidence
    if (mut._type != RA)
    {
      new_gd.add(mut);
      continue;
    }
    
    // lines only exist for polymorphisms
    if (!mut.entry_exists(POLYMORPHISM_EXISTS)) {
      new_gd.add(mut);
      continue;
    }
    mut.erase(POLYMORPHISM_EXISTS);
    
    // Copy over the values from the R output file back to the RA item
    string line;
    getline(ROUT, line);
    vector<string> line_list = split(line, "\t");

    for (uint32_t j = 0; j < header_list.size(); j++)
    {
      assert(line_list.size() > j); // die "Incorrect number of items on line:\n$line"
      mut[header_list[j]] = line_list[j];
    }

    new_gd.add(mut);
  }

  ROUT.close();

  /// Write out the file which now has much more data
  string polymorphism_statistics_ra_mc_genome_diff_file_name = settings.polymorphism_statistics_ra_mc_genome_diff_file_name;
  new_gd.write(polymorphism_statistics_ra_mc_genome_diff_file_name);

}

std::string GetWord(std::string &s) {
  RemoveLeadingWhitespace(s);
  int found = s.find_first_of(" =\n\r");
  std::string w = s.substr(0,found);
  s.erase(0, found);
  return w;
}


void RemoveLeadingWhitespace(std::string &s) {
  int found = s.find_first_not_of(" \t");
  s.erase(0,found);
}

void RemoveLeadingTrailingWhitespace(std::string &s) {
  int found = s.find_first_not_of(" \t\n\r");
  s.erase(0,found);
  found = s.find_last_not_of(" \t\n\r");
  s.erase(found+1,s.length());
}

  
/*
 Count the number of nonmatching bases in the alignment between the read and reference sequences
   Unmatched (padded) bases at the end of a read count as mismatches.
 */
uint32_t alignment_mismatches(const alignment_wrapper& a, const cReferenceSequences& ref_seq_info)
{
  bool verbose = false;
  uint32_t mismatches = 0;

  const string ref_string = ref_seq_info[a.reference_target_id()].get_sequence_1(a.reference_start_1(), a.reference_end_1());
  uint32_t ref_pos = 0;

  string read_string = a.read_char_sequence().substr(a.query_start_0(), a.query_match_length());
  uint32_t read_pos = 0;

  uint32_t* cigar_list = a.cigar_array(); // cigar array for this alignment

  if (verbose)
  {
    cout << a.read_name() << endl;
  }

  for (uint32_t i = 0; i < a.cigar_array_length(); i++)
  {
    char op = cigar_list[i] & BAM_CIGAR_MASK;
    uint32_t len = cigar_list[i] >> BAM_CIGAR_SHIFT;

    // soft padding counts as a mismatch
    if (op == BAM_CSOFT_CLIP)
    {
      mismatches += len;
    }
    else if (op == BAM_CDEL)
    {
      mismatches += len;
      ref_pos += len;
    }
    else if (op == BAM_CINS)
    {
      mismatches += len;
      read_pos += len;
    }
    else if (op == BAM_CMATCH)
    {
      for (uint32_t j = 0; j < len; j++)
      {
        if (verbose)
        {
          cout << "REF: " << ref_pos << "  " << ref_string[ref_pos] << endl;
          cout << "READ: " << read_pos << "  " << read_string[read_pos] << endl;
        }
        if (ref_string[ref_pos] != read_string[read_pos])
        {
          mismatches++;
        }
        read_pos++;
        ref_pos++;
      }
    }
    else if (op == BAM_CEQUAL) {
      read_pos++;
      ref_pos++;
    } else if (op == BAM_CDIFF) {
      mismatches++;
      read_pos++;
      ref_pos++;
    }
    else if ( (op != BAM_CHARD_CLIP) && (op != BAM_CREF_SKIP) ) {
      ERROR("Unrecognized CIGAR operation in string: " + a.cigar_string());
    }
  }

  return mismatches;
}
  
  
/*
 Sometimes scoring by bowtie2 is inconsistent (the same alignment on different strands gives a slightly different "AS")
 This function is to recalculate the score.
 
 Assumes reads were aligned with thse scoring options:
 --ma 1 --mp 3 --np 0 --rdg 2,3 --rfg 2,3 --ignore-quals";

 NOTE: For some reason bowtie2 scores are still lower than given (perhaps due to qual issues)
 */
int32_t alignment_score(const alignment_wrapper& a, const cReferenceSequences& ref_seq_info)
{
  const int32_t match_score = +1;
  const int32_t mismatch_score = -3;
  const int32_t gap_open_score = -2;
  const int32_t gap_extend_score = -3;

  
  bool verbose = false;
  int32_t alignment_score = 0;
  
  const string ref_string = ref_seq_info[a.reference_target_id()].get_sequence_1(a.reference_start_1(), a.reference_end_1());
  uint32_t ref_pos = 0;
  
  string read_string = a.read_char_sequence().substr(a.query_start_0(), a.query_match_length());
  uint32_t read_pos = 0;
  
  uint32_t* cigar_list = a.cigar_array(); // cigar array for this alignment
  
  if (verbose)
  {
    cout << a.read_name() << endl;
  }
  
  for (uint32_t i = 0; i < a.cigar_array_length(); i++)
  {
    char op = cigar_list[i] & BAM_CIGAR_MASK;
    uint32_t len = cigar_list[i] >> BAM_CIGAR_SHIFT;
    
    if (op == BAM_CDEL)
    {
      alignment_score += gap_open_score;
      alignment_score += gap_extend_score*len;
      ref_pos += len;
    }
    else if (op == BAM_CINS)
    {
      alignment_score += gap_open_score;
      alignment_score += gap_extend_score*len;
      read_pos += len;
    }
    else if (op == BAM_CMATCH)
    {
      for (uint32_t j = 0; j < len; j++)
      {
        if (verbose)
        {
          cout << "REF: " << ref_pos << "  " << ref_string[ref_pos] << endl;
          cout << "READ: " << read_pos << "  " << read_string[read_pos] << endl;
        }
        
        alignment_score += (ref_string[ref_pos] != read_string[read_pos]) ? mismatch_score : match_score;
        read_pos++;
        ref_pos++;
      }
    }
    else if (op == BAM_CEQUAL) {
      alignment_score +=  match_score;
      read_pos++;
      ref_pos++;
    } else if (op == BAM_CDIFF) {
      alignment_score +=  mismatch_score;
      read_pos++;
      ref_pos++;
    }
    else if ((op != BAM_CSOFT_CLIP) && (op != BAM_CHARD_CLIP))
    {
      ERROR("Unrecognized CIGAR operation in string: " + a.cigar_string());
    }
  }
  
  return alignment_score;
}


string shifted_cigar_string(const alignment_wrapper& a, const cReferenceSequences& ref_seq_info)
{
  bool verbose = false;

  const string ref_seq = ref_seq_info[a.reference_target_id()].get_sequence_1(a.reference_start_1(), a.reference_end_1());
  uint32_t ref_seq_index = 0;
  string read_seq = a.read_char_sequence();
  uint32_t read_seq_index = 0;
  vector<pair<char,uint16_t> > cigar_pair_array = a.cigar_pair_char_op_array();

  if (verbose)
  {
    cout << a.read_name() << endl;

    if (a.read_name() == "GW1ULQG02EIUY7")
    {
      cout << "debug" << endl;
    }
    cout << a.cigar_string() << endl;
  }

  for (vector<pair<char,uint16_t> >::iterator it = cigar_pair_array.begin(); it != cigar_pair_array.end(); it++)
  {
    char op = it->first;
    uint16_t len = it->second;

    if (op == 'D')
    {
      char base = ref_seq[ref_seq_index];
      bool all_same_base = true;
      for (uint32_t j = 1; j < len; j++)
        all_same_base = all_same_base && (ref_seq[ref_seq_index + j] == base);

      if (all_same_base)
      {
        uint16_t shift_amount = 0;
        while (ref_seq[ref_seq_index + len + shift_amount] == base)
          shift_amount++;

        if (shift_amount > 0)
        {
          if (verbose)
          {
            cout << "Shifting D: " << a.read_name() << endl;
          }

          if (it != cigar_pair_array.begin())
            (it - 1)->second += shift_amount;
          else
            cigar_pair_array.insert(it, make_pair('M', shift_amount));

          if (it != cigar_pair_array.end())
            (it + 1)->second -= shift_amount;
          else
            cigar_pair_array.push_back(make_pair('M', shift_amount));
        }

        ref_seq_index += len + shift_amount;
      }
      else
      {
        ref_seq_index += len;
      }
    }
    else if (op == 'I')
    {
      char base = read_seq[read_seq_index];
      bool all_same_base = true;
      for (uint32_t j = 1; j < len; j++)
        all_same_base = all_same_base && (read_seq[read_seq_index + j] == base);

      if (all_same_base)
      {
        uint16_t shift_amount = 0;
        while (read_seq[read_seq_index + len + shift_amount] == base)
          shift_amount++;

        if (shift_amount > 0)
        {
          if (verbose)
          {
            cout << "Shifting I: " << a.read_name() << endl;
          }

          if (it != cigar_pair_array.begin())
            (it - 1)->second += shift_amount;
          else
            cigar_pair_array.insert(it, make_pair('M', shift_amount));

          if (it != cigar_pair_array.end())
            (it + 1)->second -= shift_amount;
          else
            cigar_pair_array.push_back(make_pair('M', shift_amount));
        }

        read_seq_index += len + shift_amount;
      }
      else
      {
        read_seq_index += len;
      }
    }
    else
    {
      ref_seq_index += len;
      read_seq_index += len;
    }
  }

  stringstream shifted_cigar_string_ss;
  for (vector<pair<char,uint16_t> >::iterator it = cigar_pair_array.begin(); it != cigar_pair_array.end(); it++)
    shifted_cigar_string_ss << it->second << it->first;

  string shifted_cigar_string = shifted_cigar_string_ss.str();
  if (verbose)
  {
    cout << shifted_cigar_string << endl;
  }

  return shifted_cigar_string;
}


} // breseq namespace

