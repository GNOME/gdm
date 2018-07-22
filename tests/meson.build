m_common_test_src = [
  'm-common.c',
  's-common-address.c',
  's-common.c',
]

m_common_test_deps = [
  libgdmcommon_dep,
  libcheck_dep,
]

m_common_test = executable('m-common',
  m_common_test_src,
  dependencies: m_common_test_deps,
)

test('m-common', m_common_test)
