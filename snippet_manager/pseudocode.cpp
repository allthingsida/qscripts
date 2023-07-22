// The following snippet / pseudo-code has been kindly shared by  Arnaud Diederen from Hex-Rays

////
//// save snippets + rebuild index
////

void save_all_snippets()
{ 
  netnode main_node;
  main_node.create("$ scriptsnippets");
  
  atag = 'A'
  main_node.altdel_all(atag);
  
  // save each, plus add in index
  for i in range(snippets):
      netnode snippet_node = snippet(i).save(); // see below
      main_node.altset(i, snippet_node+1);
}
 
netnode snippet_t::save()
{
   xtag = 'X'
   if ( node == BADNODE )
     node.create();
   node.supset(0, name.c_str());
   node.supset(1, lang->name);
   node.setblob(body.c_str(), body.size(), 0, xtag); // body
   return node;
}
