API
===

OSPF=>X
-------

- init
 elsa = elsa_create(client)

- LSA change notification (received from outside / generated internally)
 elsa_lsa_changed(lsatype)
 elsa_lsa_deleted(lsatype)

[ TODO: study if computational complexity decreases much if we actually explicitly push across also information _which_ LSA changed; however, the LSA conversion might be expensive ]

- is the type covered under the ELSA interface
 elsa_is_supported(lsatype)

- dispatch (to provide access to event loop)
 elsa_dispatch()

- destroy
 elsa_destroy(elsa)


X=>OSPF
-------

- get router ID
 rid = elsai_get_rid(client)

- router ID should be changed
 elsai_change_rid(client)

- get LSAs (of type X (and rid R))
 lsa = elsai_get_lsa_by_type(client, type)
 lsa = elsai_get_lsa_by_type_next(client, lsa)

 lsa = elsai_get_lsa_by_type_rid(client, type, rid)
 lsa = elsai_get_lsa_by_type_rid_next(client, lsa)

- add LSA (=~ add+originate)
 elsai_add_lsa(client, lsa)

- remove originated LSA
 elsai_delete_lsa(client, lsa)

Look at what the structures look like under Quagga + BIRD
---------------------------------------------------------

### Quagga

- lsdb, lsa structures
    - lsdb some sort of hackish tree of lsa's
    - lsa itself identified by
      uint16 type, uint32 id+adv_router (areas=?)

- store+originate (link LSA, random example)
  ospf6_link_lsa_originate called from soft-thread
  .. malloc new LSA structure
  .. eventually call ospf6_lsa_originate 
  [ needs: lsa struct with header + body ]

### BIRD

- lsdb: 
- ospf_hash_find_header
    - ospf_hash_find (domain [== area], lsa [== Quagga id, link state id in
      RFC], rtr, type [== lsa type / Quagga type])

 [ needs: lsa struct (can be temporary), body (should be allocated
 appropriately ) ]

- store+originate: .. eventually lsa_install_new => WIY
