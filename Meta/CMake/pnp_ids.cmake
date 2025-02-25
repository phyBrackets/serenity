include(${CMAKE_CURRENT_LIST_DIR}/utils.cmake)

set(PNP_IDS_FILE pnp.ids)
set(PNP_IDS_URL http://www.uefi.org/uefi-pnp-export)
set(PNP_IDS_EXPORT_PATH ${CMAKE_BINARY_DIR}/pnp.ids.html)
set(PNP_IDS_INSTALL_PATH ${CMAKE_INSTALL_DATAROOTDIR}/${PNP_IDS_FILE})

if(ENABLE_PNP_IDS_DOWNLOAD AND NOT EXISTS ${PNP_IDS_PATH})
    message(STATUS "Downloading PNP ID database from ${PNP_IDS_URL}...")
    file(MAKE_DIRECTORY ${CMAKE_INSTALL_DATAROOTDIR})
    file(DOWNLOAD ${PNP_IDS_URL} ${PNP_IDS_EXPORT_PATH} INACTIVITY_TIMEOUT 10)

    set(PNP_IDS_HEADER LibEDID/PnpIDs.h)
    set(PNP_IDS_IMPLEMENTATION LibEDID/PnpIDs.cpp)
    set(PNP_IDS_TARGET_PREFIX LibEDID_)

    invoke_generator(
        "PnpIDsData"
        Lagom::GeneratePnpIDsData
        "${PNP_IDS_EXPORT_PATH}"
        "${PNP_IDS_TARGET_PREFIX}"
        "${PNP_IDS_HEADER}"
        "${PNP_IDS_IMPLEMENTATION}"
        arguments -p "${PNP_IDS_EXPORT_PATH}"
    )

    set(PNP_IDS_SOURCES
        ${PNP_IDS_HEADER}
        ${PNP_IDS_IMPLEMENTATION}
    )
endif()
